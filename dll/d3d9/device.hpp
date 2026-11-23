/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device, its swap chains and the surfaces they own
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "d3d9.hpp"
#include "kmcallbacks.hpp"
#include "ffpixel.hpp"
#include "ffvertex.hpp"
#include "shadervalidator.hpp"

class D9Device;
class D9SwapChain;

/* The most render targets a device binds at once */
static constexpr UINT D9MaxRenderTargets = 4;

/* Render states a device keeps a value for */
static constexpr UINT D9RenderStateCount = 256;

/* What a render state holds before the device first sets it */
static constexpr DWORD D9UnsetState = 0xBAADCAFE;

/* Kernel contexts a device tracks for its driver */
static constexpr UINT D9MaxKernelContexts = 16;

/* Presents handed to the driver that it has not reported through a callback yet */
static constexpr UINT D9QueuedPresentLimit = 8;

/* The vertex and index buffers a device keeps for drawing from application memory */
static constexpr UINT D9StagingBufferCount = 4;

/* Sixteen pixel samplers, the displacement map sampler and four vertex samplers */
static constexpr UINT D9TextureSlotCount = 21;

/* Texture stages and the stage state types a device keeps, the last one being the stage constant */
static constexpr UINT D9StageCount = 8;
static constexpr UINT D9StageStateCount = 33;

/* Sampler state types a device keeps per sampler */
static constexpr UINT D9SamplerStateCount = 14;

/* Transforms a device keeps: view, projection, eight texture transforms and 256 world matrices */
static constexpr UINT D9TransformCount = 2 + 8 + 256;

/* Vertex streams a device binds */
static constexpr UINT D9StreamCount = 16;

/* A light the application set, and whether it is on */
struct D9Light
{
    BOOLEAN Made;
    BOOLEAN Enabled;
    D3DLIGHT9 Light;
};

/* One vertex stream: its buffer, where its vertices start, their size and how often they repeat */
struct D9Stream
{
    IDirect3DVertexBuffer9* Buffer;
    UINT Offset;
    UINT Stride;
    UINT Frequency;
};

/* How a resource takes part in sharing */
enum class D9ShareMode : UINT
{
    None,
    Create,
    Open,
};

/* Usage bits the runtime adds to what a shared resource records: the resource can be locked, it was made by
   a runtime of the display driver model, and it is an offscreen plain surface */
static constexpr DWORD D9ShareUsageLockable = 0x04000000;
static constexpr DWORD D9ShareUsageModern = 0x08000000;
static constexpr DWORD D9ShareUsagePlain = 0x00400000;

/* What a sync token the driver hands back stands for: a wait for a fence value, or a signal of one, on the
   driver's contexts */
enum class D9SyncKind : UINT
{
    None,
    Wait,
    Signal,
};

/* A fence the runtime made and every token naming it keeps alive; the last reference destroys it */
struct D9TrackedFence
{
    LONG References;
    D3DKMT_HANDLE Handle;
};

VOID
D9ReleaseTrackedFence(
    _In_ D9TrackedFence* Fence);

struct D9SyncPacket
{
    D9SyncKind Kind;
    D3DKMT_HANDLE Fence;
    UINT64 Value;
    D9TrackedFence* Tracked;
};

/* What one creation asks of sharing: the mode, the handle to open and the usage the resource records */
struct D9ShareRequest
{
    D9ShareMode Mode;
    D3DKMT_HANDLE Handle;
    DWORD Usage;
};

/* A resource the driver made; its address is the runtime handle the driver passes back */
struct D9Resource
{
    HANDLE DriverResource;
    D3DKMT_HANDLE KernelResource;
    D3DDDIFORMAT Format;
    D3DDDI_POOL Pool;
    D3DDDI_RESOURCEFLAGS Flags;
    D3DDDI_RESOURCEFLAGS2 Flags2;
    UINT Width;
    UINT Height;
    UINT Fvf;

    /* Textures hand the driver every level, and cube maps every face, in one resource */
    UINT SurfaceCount;
    UINT MipLevels;

    /* Sharing: whether the resource is made to share or opened by handle, the kernel's global handle, and
       the description the kernel keeps for the runtimes that open it */
    D9ShareMode Share;
    D3DKMT_HANDLE ShareHandle;
    D3DRESOURCETYPE Type;
    DWORD ShareUsage;
    UINT Depth;
    UINT LevelCount;
    D3DDDIMULTISAMPLE_TYPE MultisampleType;
    UINT MultisampleQuality;

    /* A flip model buffer the compositor opens by its NT handle */
    BOOLEAN Composed;
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
    StreamSourceFreq,
    RenderTarget,
    DepthStencil,
    Clear,
    Present1,
    Rename,
    Blt,
    ColorFill,
    VertexShaderFunc,
    VertexShaderConst,
    VertexShaderConstI,
    VertexShaderConstB,
    PixelShaderConstI,
    PixelShaderConstB,
    Texture,
    StreamSource,
    Indices,
    VertexShaderDecl,
    PixelShader,
    DrawPrimitive,
    DrawIndexedPrimitive,
    StreamSourceUm,
    IndicesUm,
    DrawFill,
    DrawIndexedFill,
    BufBlt,
    TexBlt,
    VolBlt,
    GenerateMips,
};

/* The copies a batch records, made through the newer driver call where the driver has it */
HRESULT
D9RunBufBlt(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ HANDLE Driver,
    _In_ const D3DDDIARG_BUFFERBLT1* Blt);

HRESULT
D9RunTexBlt(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ HANDLE Driver,
    _In_ const D3DDDIARG_TEXBLT1* Blt);

HRESULT
D9RunVolBlt(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ HANDLE Driver,
    _In_ const D3DDDIARG_VOLUMEBLT1* Blt);

/* A draw the worker makes from what a batch copied into its fills */
struct D9FillDrawRecord
{
    D3DDDIARG_SETSTREAMSOURCE Stream;
    D3DDDIARG_DRAWPRIMITIVE2 Draw;
};

struct D9FillIndexedRecord
{
    D3DDDIARG_SETSTREAMSOURCE Stream;
    D3DDDIARG_SETINDICES Indices;
    D3DDDIARG_DRAWINDEXEDPRIMITIVE Draw;
};

/* Application memory a large draw reads where it lies */
struct D9MemoryRecord
{
    UINT Stride;
    const VOID* Data;
};

/* A recorded texture binding */
struct D9TextureRecord
{
    UINT Stage;
    HANDLE Resource;
};

/* How many registers of each constant kind the application may set, and how many of the vertex ones the
   driver's own pipeline holds */
struct D9ConstantLimits
{
    UINT VertexFloats;
    UINT VertexInts;
    UINT VertexBools;
    UINT PixelFloats;
    UINT PixelInts;
    UINT PixelBools;
    UINT DriverVertexFloats;
    UINT DriverVertexInts;
};

/* Where a batch remembers what is bound: targets, depth, streams, indices, then the sampler stages with the
   displacement and vertex samplers after the pixel ones */
static constexpr UINT D9BoundDepth = D9MaxRenderTargets;
static constexpr UINT D9BoundStreams = D9BoundDepth + 1;
static constexpr UINT D9BoundIndices = D9BoundStreams + 16;
static constexpr UINT D9BoundTextures = D9BoundIndices + 1;
static constexpr UINT D9BoundSlots = D9BoundTextures + 21;

/* Slots of the table that remembers which batch last used a driver resource */
static constexpr UINT D9BatchReferenceSlots = 0x1FFF;

/* A dynamic buffer draws from application memory are copied into, locked while its batch records */
struct D9BatchFill
{
    D9Resource Resource;
    VOID* Memory;
    BYTE* Data;
    UINT Used;
    UINT Size;
};

/* The fills of a recording buffer: vertices, then 16 and 32 bit indices */
static constexpr UINT D9FillVertices = 0;
static constexpr UINT D9FillIndices16 = 1;
static constexpr UINT D9FillIndices32 = 2;
static constexpr UINT D9FillCount = 3;

/* One recording buffer with the fills its draws from application memory use */
struct D9BatchBuffer
{
    BYTE* Data;
    UINT Used;
    UINT Calls;
    D9BatchFill Fills[D9FillCount];
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

    VOID
    WaitForWorker(VOID);

    VOID
    Settle(
        _In_ HANDLE Resource);

    VOID
    DrawMemory(
        _In_ const D3DDDIARG_DRAWPRIMITIVE* Draw,
        _In_ UINT Stride,
        _In_ const VOID* Vertices);

    VOID
    DrawIndexedMemory(
        _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE* Draw,
        _In_ UINT Stride,
        _In_ const VOID* Vertices,
        _In_ UINT IndexStride,
        _In_ const VOID* IndexData);

private:
    VOID
    Advance(VOID);

    static
    DWORD
    WINAPI
    WorkerMain(
        _In_ VOID* Context);

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
    UnlockFills(
        _Inout_ D9BatchBuffer* Buffer);

    VOID
    RecordMemory(
        _In_ D9BatchCall Call,
        _In_ UINT Stride,
        _In_ const VOID* Data);

    VOID
    DestroyFills(
        _Inout_ D9BatchBuffer* Buffer);

    D9Device* Device;
    const D3DDDI_DEVICEFUNCS* Funcs;
    HANDLE Driver;
    D9BatchBuffer Buffers[2] = {};
    UINT Current = 1;
    HANDLE Thread = NULL;
    HANDLE Wake = NULL;
    HANDLE Done = NULL;
    volatile LONG Busy = 0;
    BOOLEAN Pending = FALSE;
    BOOLEAN Exiting = FALSE;

    /* Each handed over or drained batch starts a generation; a resource remembers the last one that used it */
    UINT64 Generation = 0;
    BYTE References[D9BatchReferenceSlots] = {};

    /* What stays bound for every new generation: the targets, the depth buffer, the streams, the indices and
       the textures */
    HANDLE Bound[D9BoundSlots] = {};
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

/* What a swap chain surface is to the compositor: nothing, the small target presents clear, or a buffer it
   shows */
enum class D9SurfaceRole : UINT
{
    Plain,
    CompositionTarget,
    CompositionBuffer,
};

/* The compositor entry points flip model chains use; win32u and dwmapi export them without a header */
struct D9CompositionCalls
{
    NTSTATUS (APIENTRY* Bind)(HANDLE, DWORD, DWORD, VOID*, VOID*, VOID*);
    NTSTATUS (APIENTRY* Unbind)(HANDLE, DWORD);
    NTSTATUS (APIENTRY* Notify)(HANDLE, D3DKMT_PRESENTHISTORYTOKEN*);
    NTSTATUS (APIENTRY* QueryStatistics)(HANDLE, VOID*);
    HRESULT (WINAPI* CreateSurfaceHandle)(DWORD, SECURITY_ATTRIBUTES*, HANDLE*);
    HRESULT (WINAPI* AllocateSecurity)(PSECURITY_DESCRIPTOR*, DWORD);
    VOID (WINAPI* FreeSecurity)(PSECURITY_DESCRIPTOR);
};

/* Resolved once; any entry may be missing */
const D9CompositionCalls*
D9Composition(VOID);

/* What a flip model chain hands the device for one present */
struct D9ComposedPresent
{
    HANDLE Surface;
    UINT64 BindingId;
    UINT64 FenceValue;
    HANDLE FenceHandle;
    UINT SwapChainIndex;
    UINT FlipInterval;
    DWORD Flags;
    const RGNDATA* DirtyRegion;
    UINT Width;
    UINT Height;
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

    /* Whether the application can lock the surface, through its texture for a level */
    BOOLEAN
    CanLock() const;

    /* The runtime holds the surface without a reference the application sees, as the cursor does */
    VOID
    Pin(VOID);

    VOID
    Unpin(VOID);

    /* A surface made to share, or opened by handle, says so before Create */
    VOID
    Share(
        _In_ const D9ShareRequest* Request)
    {
        Sharing = *Request;
    }

    D3DKMT_HANDLE
    ShareHandle() const
    {
        return Resource != nullptr ? Resource->ShareHandle : 0;
    }

    /* A flip model swap chain says what the surface is for before Create */
    VOID
    AssignRole(
        _In_ D9SurfaceRole Assigned)
    {
        Role = Assigned;
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
    BOOLEAN LockedAsync = FALSE;
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
    D9ShareRequest Sharing = {};
    D9SurfaceRole Role = D9SurfaceRole::Plain;
    LONG PinCount = 0;
    BOOLEAN Orphaned = FALSE;

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

    /* Sharing, or the application memory a system memory texture keeps its only level in */
    D9ShareRequest Share;
    VOID* UserMemory;
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
    BOOLEAN LockedAsync;
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

    BOOLEAN
    IsLockable() const
    {
        return Lockable;
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

    D3DFORMAT
    FormatOf() const
    {
        return Format;
    }

    D9TextureKind
    KindOf() const
    {
        return Kind;
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

    /* The resource a draw binds; a managed texture makes or refreshes its video copy first */
    D9Resource*
    DrawResource(
        _Out_ BOOLEAN* Made);

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
    D9ShareRequest Sharing;
    BOOLEAN UserMemory;
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

/* The cursor of one swap chain; each reset of the chain starts it over */
class D9Cursor
{
public:
    D9Cursor() = default;

    D9Cursor(const D9Cursor&) = delete;
    D9Cursor& operator=(const D9Cursor&) = delete;

    VOID
    Start(
        _In_ D9Device* Owner,
        _In_ INT X,
        _In_ INT Y,
        _In_ BOOLEAN IsWindowed);

    /* Lets go of the image and everything made for the cursor, as a reset or the swap chain going away does */
    VOID
    Release(VOID);

    HRESULT
    SetProperties(
        _In_ UINT HotX,
        _In_ UINT HotY,
        _In_ D9Surface* Bitmap);

    VOID
    SetPosition(
        _In_ INT X,
        _In_ INT Y);

    BOOL
    SetVisibility(
        _In_ BOOL Show);

private:
    VOID
    Destroy(VOID);

    HRESULT
    MakeDriverSurfaces(
        _In_ const D3DSURFACE_DESC* Desc);

    D9Device* Device = nullptr;
    HCURSOR Icon = NULL;
    INT PositionX = 0;
    INT PositionY = 0;
    UINT Trails = 0;
    BOOLEAN Windowed = TRUE;

    /* The image the cursor holds until the next one replaces it */
    D9Surface* Held = nullptr;

    /* The surfaces the runtime draws a cursor with when the window manager cannot, and the size made for */
    D9Resource Surfaces[3] = {};
    UINT SurfaceWidth = 0;
    UINT SurfaceHeight = 0;

    /* Set once the window manager has the cursor image, and while the application wants it shown */
    BOOLEAN Hardware = FALSE;
    BOOLEAN Visible = FALSE;
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

    /* The ramp a windowed chain keeps for GetGammaRamp; the display's gamma is left alone */
    D3DGAMMARAMP&
    GammaRamp()
    {
        return Gamma;
    }

    D9Cursor&
    CursorState()
    {
        return Cursor;
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

    BOOLEAN
    MatchesDirectFlip(VOID) const;

    HRESULT
    CreateClearTargets(VOID);

    HRESULT
    CreateComposition(
        _In_ PSECURITY_DESCRIPTOR Security,
        _In_ BOOLEAN DirectFlip);

    HRESULT
    BindComposition(VOID);

    VOID
    ReleaseComposition(VOID);

    HRESULT
    PresentComposed(
        _In_opt_ const RGNDATA* DirtyRegion,
        _In_ DWORD Flags);

    HRESULT
    ComposedStats(
        _Out_ D3DPRESENTSTATS* Stats);

public:
    /* Writing a buffer of the chain first waits for the compositor to give it back */
    VOID
    AcquireWriteAccess(
        _In_ const D9Surface* Surface);

private:

    UINT Index;
    HWND FocusWindow;
    D3DPRESENT_PARAMETERS Parameters = {};
    /* Flip swap chains keep one buffer more than they report, the one on screen */
    D9Surface* BackBuffers[D3DPRESENT_BACK_BUFFERS_MAX_EX + 1] = {};
    UINT BackBufferCount = 0;
    D3DGAMMARAMP Gamma;
    D9Cursor Cursor;

    /* Present statistics of a windowed flip chain: presents made, the count last reported, and whether the
       next report must say the statistics broke off */
    UINT FlipPresents = 0;
    UINT StatsReported = 0;
    BOOLEAN StatsDisjoint = FALSE;

    /* The compositor's view of a flip model chain: the NT handle of each buffer, the target and depth buffer
       presents clear, the fence and its handle, the composition surface and its binding */
    BOOLEAN Composed = FALSE;
    HANDLE BufferHandles[D3DPRESENT_BACK_BUFFERS_MAX_EX + 1] = {};
    D9Surface* ClearTarget = nullptr;
    D9Surface* ClearDepth = nullptr;
    D9TrackedFence* Fence = nullptr;
    HANDLE FenceHandle = NULL;
    UINT64 FenceValue = 0;
    UINT64 FenceWaited = 0;
    HANDLE CompositionSurface = NULL;
    UINT64 BindingId = 0;
    BOOLEAN DirectFlipBound = FALSE;
    UINT ComposedIndex = 0;

    /* What the compositor last reported, and the device present count when this chain last presented */
    D3DPRESENTSTATS CachedStats = {};
    D3DPRESENTSTATS LastStats = {};
    UINT PresentMark = 0;
    LARGE_INTEGER PresentTime = {};
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
        _In_ D3DDDI_POOL Placement,
        _In_opt_ const D9ShareRequest* Share = nullptr);

    D3DKMT_HANDLE
    ShareHandle() const
    {
        return Resource != nullptr ? Resource->ShareHandle : 0;
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

    /* A stream or the index binding holds the buffer past its last release */
    VOID
    Bind(VOID)
    {
        BindCount += 1;
    }

    VOID
    Unbind(VOID);

    /* The resource a draw binds; a managed buffer makes or refreshes its video copy first */
    D9Resource*
    DrawResource(
        _Out_ BOOLEAN* Made);

    /* Bytes per index of an index buffer */
    UINT
    IndexSize() const
    {
        return Format == D3DFMT_INDEX32 ? 4 : 2;
    }

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

    LONG BindCount = 0;
    BOOLEAN Orphaned = FALSE;
};

typedef D9Buffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC> D9VertexBuffer;
typedef D9Buffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC> D9IndexBuffer;

/* Registers one shader's DEF instructions may fix, of each kind */
static constexpr UINT D9MaxShaderDefs = 256;

/* What the token check learns of a shader: its sizes with and without comments, the registers it defines and,
   for a vertex shader, a vertex format describing the outputs it writes and which vertex processing can run it */
struct D9ShaderLayout
{
    UINT Size;
    DWORD Outputs;
    BOOLEAN HardwareCapable;
    BOOLEAN SoftwareCapable;
    UINT StrippedSize;
    UINT Instructions;
    UINT FloatDefCount;
    UINT IntDefCount;
    UINT BoolDefCount;
    USHORT FloatDefs[D9MaxShaderDefs];
    USHORT IntDefs[D9MaxShaderDefs];
    USHORT BoolDefs[D9MaxShaderDefs];
};

BOOLEAN
D9CheckShaderTokens(
    _In_ const DWORD* Tokens,
    _In_ BOOLEAN Pixel,
    _In_ const D9_SHADER_CHECK_DEVICE* Device,
    _Out_ D9ShaderLayout* Layout);

/* A vertex or pixel shader: the function the application gave, what the driver made of it and the constants
   its DEF instructions fix */
template <class Interface>
class D9Shader final : public Interface, public D9Child
{
public:
    explicit
    D9Shader(
        _In_ D9Device* Owner)
        : D9Child(Owner)
    {
    }

    ~D9Shader();

    D9Shader(const D9Shader&) = delete;
    D9Shader& operator=(const D9Shader&) = delete;

    HRESULT
    Initialize(
        _In_ const DWORD* Function,
        _In_ const D9ShaderLayout* Shape);

    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override;

    STDMETHOD_(ULONG, AddRef)() override;

    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(GetDevice)(
        _Out_ IDirect3DDevice9** Result) override;

    STDMETHOD(GetFunction)(
        _Out_writes_bytes_opt_(*SizeOfData) VOID* Data,
        _Inout_ UINT* SizeOfData) override;

    /* The device and state blocks keep a shader past its last release */
    VOID
    Bind(VOID)
    {
        BindCount += 1;
    }

    VOID
    Unbind(VOID);

    const D9ShaderLayout*
    Shape() const
    {
        return &Layout;
    }

    const DWORD*
    DriverCode() const
    {
        return Code;
    }

    HANDLE DriverHandle = NULL;

private:
    DWORD* Function = nullptr;
    DWORD* Code = nullptr;
    D9ShaderLayout Layout = {};
    LONG BindCount = 0;
    BOOLEAN Orphaned = FALSE;
};

typedef D9Shader<IDirect3DVertexShader9> D9VertexShader;
typedef D9Shader<IDirect3DPixelShader9> D9PixelShader;

/* A vertex declaration: its elements, the format the fixed function pipeline reads it as and the driver's copy */
class D9Declaration final : public IDirect3DVertexDeclaration9, public D9Child
{
public:
    explicit
    D9Declaration(
        _In_ D9Device* Owner)
        : D9Child(Owner)
    {
    }

    ~D9Declaration();

    D9Declaration(const D9Declaration&) = delete;
    D9Declaration& operator=(const D9Declaration&) = delete;

    HRESULT
    Initialize(
        _In_reads_(Count + 1) const D3DVERTEXELEMENT9* Source,
        _In_ UINT Count,
        _In_ DWORD Format);

    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override;

    STDMETHOD_(ULONG, AddRef)() override;

    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(GetDevice)(
        _Out_ IDirect3DDevice9** Result) override;

    STDMETHOD(GetDeclaration)(
        _Out_writes_opt_(*NumElements) D3DVERTEXELEMENT9* Element,
        _Inout_ UINT* NumElements) override;

    VOID
    Bind(VOID)
    {
        BindCount += 1;
    }

    VOID
    Unbind(VOID);

    UINT ElementCount = 0;
    D3DVERTEXELEMENT9 Elements[MAXD3DDECLLENGTH + 1] = {};
    DWORD Fvf = 0;
    HANDLE DriverHandle = NULL;

    /* The vertices hold transformed positions, which no vertex shader processes */
    BOOLEAN Transformed = FALSE;

    /* The device made the declaration for a vertex format */
    BOOLEAN ForFormat = FALSE;

    /* Declarations the device made for formats are kept in a list and live until the device goes */
    D9Declaration* NextKept = nullptr;
    BOOLEAN Orphaned = FALSE;

private:
    LONG BindCount = 0;
};

/* hardware.cpp */
VOID
D9ApplyShare(
    _Inout_ D9Resource* Resource,
    _In_opt_ const D9ShareRequest* Share,
    _In_ D3DRESOURCETYPE Type);

/* device.cpp */
VOID
D9StartShare(
    _In_opt_ const HANDLE* SharedHandle,
    _In_ DWORD Usage,
    _Out_ D9ShareRequest* Share);

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

BOOLEAN
D9IsRetiredState(
    _In_ DWORD State);

/* texture.cpp */
D9TextureCore*
D9CoreOf(
    _In_ IDirect3DBaseTexture9* Texture);

VOID
D9HoldTexture(
    _In_ IDirect3DBaseTexture9* Texture,
    _In_ BOOLEAN Hold);

/* What one state block entry sets */
enum class D9BlockOp : UINT
{
    RenderState,
    StageState,
    SamplerState,
    Texture,
    StreamSource,
    StreamFrequency,
    Indices,
    NPatchMode,
    Viewport,
    Scissor,
    Transform,
    ClipPlane,
    Material,
    Light,
    LightEnable,
    Fvf,
    VertexFloats,
    VertexInts,
    VertexBools,
    PixelFloats,
    PixelInts,
    PixelBools,
    VertexShader,
    PixelShader,
    Declaration,
};

/* An entry's header; the values it sets follow, padded to eight bytes */
struct D9BlockEntry
{
    D9BlockOp Op;
    UINT Index;
    UINT Type;
    UINT Size;
};

/* A stream binding as a block keeps it */
struct D9BlockStream
{
    IDirect3DVertexBuffer9* Buffer;
    UINT Offset;
    UINT Stride;
};

/* The settings a block replays in the order they were recorded or gathered; it holds the textures and
   buffers it names as a binding would */
class D9StateBlock final : public IDirect3DStateBlock9, public D9Child
{
public:
    explicit
    D9StateBlock(
        _In_ D9Device* Owner)
        : D9Child(Owner)
    {
    }

    ~D9StateBlock();

    D9StateBlock(const D9StateBlock&) = delete;
    D9StateBlock& operator=(const D9StateBlock&) = delete;

    HRESULT
    Add(
        _In_ D9BlockOp Op,
        _In_ UINT Index,
        _In_ UINT Type,
        _In_reads_bytes_(Size) const VOID* Values,
        _In_ UINT Size);

    /* Walks the entries; Next gives the first with null and stops with null */
    _Ret_maybenull_
    D9BlockEntry*
    Next(
        _In_opt_ D9BlockEntry* Entry) const;

    static
    VOID
    Hold(
        _In_ const D9BlockEntry* Entry,
        _In_ BOOLEAN Take);

    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override;

    STDMETHOD_(ULONG, AddRef)() override;

    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(GetDevice)(
        _Out_ IDirect3DDevice9** Result) override;

    STDMETHOD(Capture)() override;

    STDMETHOD(Apply)() override;

private:
    BYTE* Entries = nullptr;
    UINT Used = 0;
    UINT Capacity = 0;
};

/* The values that follow an entry's header */
inline
VOID*
D9BlockValues(
    _In_ D9BlockEntry* Entry)
{
    return Entry + 1;
}

DWORD
D9DriverStageValue(
    _In_ UINT Type,
    _In_ DWORD Value);

/* Changes the shader switch acts on at the next draw: the vertex input became transformed or not, the
   fixed function or the application's vertex shader is wanted, the converters' inputs changed, and the
   fixed function or the application's pixel shader is wanted */
static constexpr UINT D9SwitchTransformedInput = 0x0004;
static constexpr UINT D9SwitchShaderInput = 0x0008;
static constexpr UINT D9SwitchWantVertexConverter = 0x0010;
static constexpr UINT D9SwitchWantApiVertexShader = 0x0020;
static constexpr UINT D9SwitchPixelInput = 0x0040;
static constexpr UINT D9SwitchVertexInput = 0x0080;
static constexpr UINT D9SwitchClipPlanes = 0x0100;
static constexpr UINT D9SwitchWantPixelConverter = 0x0200;
static constexpr UINT D9SwitchWantApiPixelShader = 0x0400;
static constexpr UINT D9SwitchPixelShaderUse = 0x0800;
static constexpr UINT D9SwitchPointSize = 0x1000;
static constexpr UINT D9SwitchPointList = 0x2000;

/* Decides at each draw whether the application's shaders or the shaders the runtime makes for the fixed
   function pipeline reach the driver, and what each side still has to hear */
struct D9ShaderSwitch
{
    /* Changes waiting, and the ones the current modes act on */
    UINT Pending;
    UINT Allowed;

    /* The fixed function vertex shader is bound; the fixed function pixel shader is bound when the mode is not 0 */
    BOOLEAN VertexConverting;
    UINT PixelMode;

    /* Which converters the driver's shader versions allow */
    BOOLEAN HasVertexConverter;
    BOOLEAN HasPixelConverter;

    /* The vertex converter reads untransformed vertices of a declaration */
    BOOLEAN ShaderInput;

    /* The last draw drew points */
    BOOLEAN DrewPoints;

    /* The application's shaders and the inputs they read */
    HANDLE ApiVertexShader;
    UINT ApiVertexInputs;
    HANDLE ApiPixelShader;
    INT ApiPixelInputs;

    /* Pixel constants the application set while the fixed function pixel shader was bound, restored when its
       own shader comes back, and the vertex constants the vertex converter wrote over */
    UINT PixelConstantMark;
    UINT VertexFloatsUsed;
    UINT VertexIntsUsed;

    /* What draws read their vertices as: a format, or a declaration */
    DWORD InputFvf;
    HANDLE InputDeclaration;
    DWORD PixelFvf;
    HANDLE PixelDeclaration;

    /* The layout transformed vertices hand the pixel converter, and the converter's shader the driver has */
    const D9Declaration* PixelLayout;
    HANDLE ConverterBound;

    /* What the application set for each stage's coordinates and transform and for each wrap state, the wraps
       the driver has by stage, and the stages the vertex converter renumbers or projects only */
    DWORD ApiCoordIndex[8];
    DWORD ApiTransformFlags[8];
    DWORD ApiWrap[8];
    DWORD DriverWrap[8];
    UINT RenumberedStages;
    UINT TransformedStages;

    /* The enabled clip planes, and those the driver has in clip space for the vertex converter */
    DWORD ClipPlaneEnable;
    UINT ClipPlanesMoved;
};

/* Draw state that changed since the last draw and reaches the driver before the next one */
static constexpr UINT D9DrawTextures = 0x0002;
static constexpr UINT D9DrawVertexInput = 0x0200;
static constexpr UINT D9DrawStreams = 0x0800;

/* The stream dirty bit of the index binding */
static constexpr UINT D9IndexStreamBit = 0x10000;

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

    /* Shader objects make and free their driver shaders through the device */
    HRESULT
    CreateDriverShader(
        _In_ BOOLEAN Pixel,
        _In_reads_bytes_(Size) const DWORD* Code,
        _In_ UINT Size,
        _Out_ HANDLE* Handle);

    VOID
    DestroyDriverShader(
        _In_ BOOLEAN Pixel,
        _In_ HANDLE Handle);

    VOID
    DestroyDriverDeclaration(
        _In_ HANDLE Handle);

    /* State blocks replay and gather through the device's own calls */
    HRESULT
    ApplyBlock(
        _In_ D9StateBlock* Block);

    HRESULT
    CaptureBlock(
        _In_ D9StateBlock* Block);

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

    /* Present statistics need the extended object and a device made to keep them, that a failed reset did not stop */
    BOOLEAN
    KeepsPresentStats() const
    {
        return !NotReset && Enum->IsExtended() && (BehaviorFlags & D3DCREATE_ENABLE_PRESENTSTATS) != 0;
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
        _In_opt_ const D9ShareRequest* Share,
        _Out_ D9Resource* Resource,
        _In_ D9SurfaceRole Role = D9SurfaceRole::Plain);

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
        _Out_ D3DLOCKED_RECT* Locked,
        _Out_ BOOLEAN* Asynchronous);

    HRESULT
    UnlockDriverSurface(
        _In_ D9Resource* Resource,
        _In_ BOOLEAN Asynchronous);

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
        _Out_ D3DLOCKED_BOX* Locked,
        _Out_ BOOLEAN* Asynchronous);

    HRESULT
    UnlockDriverSubresource(
        _In_ D9Resource* Resource,
        _In_ UINT Index,
        _In_ BOOLEAN Asynchronous);

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
        _In_opt_ const D9ShareRequest* Share,
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

    /* A flip model present: the back buffer goes to the compositor with the token the chain describes */
    HRESULT
    PresentToCompositor(
        _In_ D9Resource* Source,
        _In_ const D9ComposedPresent* Present);

    /* The semaphore flip model chains share; each chain that binds holds it once */
    HANDLE
    AcquireFlipSemaphore(VOID);

    VOID
    ReleaseFlipSemaphore(VOID);

    UINT
    PresentCount() const
    {
        return PresentsMade;
    }

    LARGE_INTEGER
    PresentTime() const
    {
        return LastPresentTime;
    }

    /* Whether as many presents as the limit allows are still in flight */
    BOOLEAN
    PresentLimitReached(VOID);

    /* A surface about to be written waits until the compositor let go of it, when a flip model chain owns it */
    VOID
    AcquireWriteAccess(
        _In_opt_ D9Surface* Surface);

    /* Has the driver wait on its contexts until a fence reaches a value */
    VOID
    WaitOnFence(
        _In_ D9TrackedFence* Fence,
        _In_ UINT64 Value);

    /* Swaps the bound targets and depth buffer with the ones given, as presents of the flip model do */
    VOID
    ExchangeTargets(
        _Inout_updates_(D9MaxRenderTargets) D9Surface** Targets,
        _Inout_ D9Surface** Depth);

    VOID
    ReleaseWindowSurface(VOID);

    /* A flip moved the surfaces behind the bound targets */
    VOID
    NoteTargetsMoved(VOID)
    {
        TargetsDirty |= 1;
        DepthDirty = TRUE;
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

    /* Sync tokens: packets the runtime hands the driver by number, which the driver passes back to run */
    HRESULT
    AcquireSyncToken(
        _In_ const D9SyncPacket* Packet,
        _Out_ UINT64* Token);

    HRESULT
    TakeSyncPacket(
        _In_ UINT64 Token,
        _Out_ D9SyncPacket* Packet);

    HRESULT
    RunSyncToken(
        _In_ const D3DDDICB_SYNCTOKEN* Args,
        _In_ D9SyncKind Expected);

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
    SendResetViewport(VOID);

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

    BOOLEAN
    KeepsNoState(VOID) const;

    VOID
    SendStateValue(
        _In_ UINT State,
        _In_ DWORD Value);

    VOID
    SendStageValue(
        _In_ UINT Stage,
        _In_ UINT Type,
        _In_ DWORD Value);

    VOID
    SendSamplerValue(
        _In_ UINT Sampler,
        _In_ UINT Type,
        _In_ DWORD Value);

    VOID
    SendWInfo(
        _In_ const D3DMATRIX* Projection);

    VOID
    SendClipPlane(
        _In_ UINT Index,
        _In_ UINT Repeats);

    VOID
    SendStreamFrequency(
        _In_ UINT Stream);

    VOID
    SwitchVertexProcessing(
        _In_ BOOLEAN Software);

    VOID
    UnbindStreams(VOID);

    VOID
    SendInitialState(VOID);

    HRESULT
    CreateAutoDepthStencil(
        _In_ const D3DPRESENT_PARAMETERS* Parameters);

    VOID
    BindRenderTargets(VOID);

    VOID
    UnpinTargets(VOID);

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
    DdiShaderConstants(
        _In_ D9BatchCall Call,
        _In_ UINT Register,
        _In_ UINT Count,
        _In_reads_bytes_opt_(Size) const VOID* Data,
        _In_ UINT Size);

    HRESULT
    CreateConstants(VOID);

    HRESULT
    FillBlock(
        _Inout_ D9StateBlock* Block,
        _In_ D3DSTATEBLOCKTYPE Type);

    HRESULT
    RecordConstants(
        _In_ D9BlockOp Op,
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
        _In_ UINT Stride);

    VOID
    ClearConstants(VOID);

    VOID
    SendAroundDefs(
        _In_ D9BatchCall Call,
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
        _In_ UINT Stride,
        _In_reads_(DefCount) const USHORT* Defs,
        _In_ UINT DefCount);

    BOOLEAN
    DriverHasVertexShaders(VOID) const;

    HRESULT
    DriverDeclaration(
        _In_ const D9Declaration* Made,
        _Out_ HANDLE* Handle);

    HRESULT
    BindFvfDeclaration(
        _In_ DWORD Fvf);

    VOID
    ReleaseFvfDeclarations(VOID);

    VOID
    SendVertexConstants(
        _In_ D9BatchCall Call,
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
        _In_ UINT Stride);

    VOID
    SendPixelConstants(
        _In_ D9BatchCall Call,
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
        _In_ UINT Stride);

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
    DdiStreamSourceFreq(
        _In_ const D3DDDIARG_SETSTREAMSOURCEFREQ* Args);

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
    OpenSharedResource(
        _Inout_ D9Resource* Resource);

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
        _In_ const D9ShareRequest* Share,
        _Inout_opt_ HANDLE* SharedHandle,
        _Out_ IDirect3DSurface9** Surface);

    BOOLEAN
    CanShare(VOID) const;

    HRESULT
    CheckDriverRestriction(VOID);

    VOID
    QueryProtection(VOID);

    HRESULT
    LockAsyncResource(
        _Inout_ D3DDDIARG_LOCK* Request,
        _In_ UINT LockCount);

    BOOLEAN
    LocksAsync(
        _In_ const D9Resource* Resource) const;

    HRESULT
    LockDriverResource(
        _In_ D9Resource* Resource,
        _Inout_ D3DDDIARG_LOCK* Args,
        _Out_ BOOLEAN* Asynchronous);

    HRESULT
    UnlockDriverResource(
        _In_ D9Resource* Resource,
        _In_ UINT Index,
        _In_ BOOLEAN Asynchronous);

    template <class Buffer>
    HRESULT
    AddBuffer(
        _In_ UINT Length,
        _In_ DWORD Usage,
        _In_ D3DFORMAT Format,
        _In_ DWORD Fvf,
        _In_ D3DPOOL Pool,
        _In_ D3DPOOL Placed,
        _Inout_opt_ HANDLE* SharedHandle,
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

    /* draw.cpp */
    VOID
    StartShaderSwitch(VOID);

    VOID
    UpdateSwitch(
        _In_ UINT Set,
        _In_ UINT Cleared);

    VOID
    SwitchVertexShader(
        _In_opt_ HANDLE Shader,
        _In_ UINT Inputs);

    VOID
    SwitchPixelShader(
        _In_opt_ HANDLE Shader,
        _In_ INT Inputs);

    VOID
    SwitchDeclaration(
        _In_ const D9Declaration* Bound);

    VOID
    FeedPixelTexture(
        _In_ UINT Stage,
        _In_opt_ IDirect3DBaseTexture9* Texture);

    VOID
    PreparePixelShader(VOID);

    VOID
    PrepareVertexShader(VOID);

    VOID
    StartVertexConverter(VOID);

    VOID
    ReleaseVertexShaders(VOID);

    VOID
    SendConverterRanges(VOID);

    VOID
    SendConverterClipPlanes(VOID);

    VOID
    RenumberStages(
        _In_ BOOLEAN Converting);

    BOOLEAN
    FeedVertexStage(
        _In_ UINT Stage,
        _In_ UINT Type,
        _Inout_ DWORD* Value);

    VOID
    StartPixelConverter(VOID);

    VOID
    ReleasePixelShaders(VOID);

    VOID
    SettleSwitch(
        _In_ D3DPRIMITIVETYPE PrimitiveType,
        _In_ BOOLEAN Indexed);

    VOID
    ApplySwitch(VOID);

    VOID
    MarkSceneState(VOID);

    HRESULT
    CheckFirstDraw(
        _In_ BOOLEAN Indexed,
        _In_ BOOLEAN FromMemory);

    HRESULT
    FlushVertexInput(VOID);

    VOID
    FlushTextures(VOID);

    VOID
    FlushStreams(VOID);

    VOID
    SendPixelFloats(
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_(Count * 4) const FLOAT* Data);

    VOID
    RestoreShaderDefs(
        _In_ BOOLEAN Pixel,
        _In_ const D9ShaderLayout* Old,
        _In_opt_ const D9ShaderLayout* New);

    VOID
    DdiTexture(
        _In_ UINT Stage,
        _In_opt_ HANDLE Resource);

    VOID
    DdiStreamSource(
        _In_ const D3DDDIARG_SETSTREAMSOURCE* Args);

    VOID
    DdiIndices(
        _In_ const D3DDDIARG_SETINDICES* Args);

    VOID
    DdiVertexShaderDecl(
        _In_ HANDLE Handle);

    VOID
    DdiPixelShader(
        _In_opt_ HANDLE Handle);

    HRESULT
    DdiDrawPrimitive(
        _In_ const D3DDDIARG_DRAWPRIMITIVE* Args);

    HRESULT
    DdiDrawIndexedPrimitive(
        _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE* Args);

    HRESULT
    ResetCommon(
        _Inout_ D3DPRESENT_PARAMETERS* Parameters,
        _In_opt_ const D3DDISPLAYMODEEX* FullscreenMode);

    HRESULT
    DriverPresent(
        _In_opt_ HANDLE SourceResource,
        _In_opt_ HANDLE TargetResource,
        _In_ UINT DriverFlags);

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

    /* A flip handed to the driver that has not reached the kernel yet; the next flip flushes it out first */
    volatile LONG FlipQueued = 0;

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

    DWORD StageStates[D9StageCount][D9StageStateCount];
    DWORD SamplerStates[D9TextureSlotCount][D9SamplerStateCount];

    /* Fixed function state the driver never sees directly */
    D3DMATRIX Transforms[D9TransformCount];
    D3DMATERIAL9 CurrentMaterial = {};
    D9Light* Lights = nullptr;
    UINT LightCapacity = 0;
    FLOAT ClipPlanes[D3DMAXUSERCLIPPLANES][4] = {};
    D3DCLIPSTATUS9 CurrentClipStatus = { 0, 0xFFFFFFFF };
    FLOAT PatchSegments = 0.0f;

    /* The viewport and scissor rectangle as last set, with any depth range fix the runtime made */
    D3DVIEWPORT9 CurrentViewport = {};
    RECT ScissorRect = {};

    /* What vertices and indices come from */
    DWORD CurrentFvf = 0;
    D9Stream Streams[D9StreamCount] = {};
    IDirect3DIndexBuffer9* Indices = nullptr;

    /* Stream frequencies set while vertices went through the software pipeline, which the driver has not seen */
    BOOLEAN FrequenciesPending = FALSE;

    /* Vertices go through the runtime's software pipeline; a mixed device switches this */
    BOOLEAN SoftwareVertices;
    INT GpuPriority = 0;
    UINT FrameLatency = 3;

    /* Flip model chains share one semaphore that limits presents in flight; the device counts every present */
    HANDLE FlipSemaphore = NULL;
    UINT FlipSemaphoreUsers = 0;
    UINT64 FlipSemaphoreId = 0;
    UINT PresentsMade = 0;
    LARGE_INTEGER LastPresentTime = {};

    /* The kernel's limit of queued presents, and how far it moved since the semaphore last followed it */
    UINT PresentLimit = 3;
    INT PresentLimitChange = 0;

    /* Copies of PendingPresent the driver may report later from its own thread */
    SRWLOCK PresentQueueLock = SRWLOCK_INIT;
    D3DKMT_PRESENT QueuedPresents[D9QueuedPresentLimit] = {};
    UINT QueuedPresentFirst = 0;
    UINT QueuedPresentCount = 0;

    /* The bound targets reach the driver before the first operation that writes them, one bit per target index,
       with the viewport and scissor rectangle a new first target brings; targets the application set are pinned
       while bound */
    ULONG TargetsDirty = 0;
    BOOLEAN DepthDirty = FALSE;
    BOOLEAN ViewportPending = FALSE;
    BOOLEAN TargetPinned[D9MaxRenderTargets] = {};
    BOOLEAN DepthPinned = FALSE;
    D9Resource StagingBuffers[D9StagingBufferCount] = {};

    /* Content protection caps the driver reported, asked for once when a surface first wants them */
    BOOLEAN ProtectionQueried = FALSE;
    BOOLEAN ProtectionSupported = FALSE;
    BOOLEAN ProtectionSoftware = FALSE;

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

    /* Shader constants as the application set them; vertex ones a mixed or software device keeps until a draw
       on the hardware pipeline needs them */
    D9ConstantLimits Limits = {};
    FLOAT (*VertexFloats)[4] = nullptr;
    INT (*VertexInts)[4] = nullptr;
    BOOL* VertexBools = nullptr;
    FLOAT (*PixelFloats)[4] = nullptr;
    INT (*PixelInts)[4] = nullptr;
    BOOL* PixelBools = nullptr;
    BOOLEAN VertexConstantsPending = FALSE;

    /* The block BeginStateBlock started; while it is set, state calls go into it and leave the device alone */
    D9StateBlock* Recording = nullptr;

    /* The shaders the application bound */
    D9VertexShader* VertexShader = nullptr;
    D9PixelShader* PixelShader = nullptr;

    /* The bound declaration, and those made for formats */
    D9Declaration* Declaration = nullptr;
    D9Declaration* FvfDeclarations = nullptr;

    /* What changed since the last draw, the samplers and streams among it, and whether a draw got through the
       checks the first one makes */
    UINT DrawDirty = 0;
    UINT TextureDirty = 0;
    UINT StreamDirty = 0;
    BOOLEAN DrawChecked = FALSE;

    /* The format SetFVF set, which a declaration clears, and the last one set, which it keeps */
    DWORD ApiFvf = 0;
    DWORD LastFvf = 0;

    /* Bytes per index of the bound index buffer, as the driver last needs to hear them */
    UINT IndexStride = 0;

    /* The application's shaders or the fixed function converters' */
    D9ShaderSwitch Switch = {};

    /* The pixel shaders standing in for the texture stages */
    D9PixelConverter PixelShaders;

    /* The vertex shaders standing in for the fixed function vertex pipeline, and whether vertex fog is on */
    D9VertexConverter VertexShaders;
    BOOLEAN VertexFogged = FALSE;

    /* Sync token packets, with the slots tokens the driver ran left free */
    SRWLOCK SyncLock = SRWLOCK_INIT;
    D9SyncPacket* SyncPackets = nullptr;
    UINT SyncPacketCount = 0;
    UINT SyncPacketCapacity = 0;
    UINT* SyncFreeSlots = nullptr;
    UINT SyncFreeCount = 0;
    UINT SyncFreeCapacity = 0;
};
