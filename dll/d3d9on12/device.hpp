/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The Direct3D 9 device on top of the translation layer's immediate context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "d3d9on12.hpp"
#include "scfinal.hpp"

/* A piece of an upload buffer: where the GPU and the CPU see it */
/* Clears what the translation layer still has bound before letting a resource go */
struct NineUnderlyingDeleter
{
    VOID
    operator()(
        _In_opt_ TL::Resource* Resource) const;
};

typedef std::unique_ptr<TL::Resource, NineUnderlyingDeleter> NineUnderlying;

struct NineUploadSlice
{
    TL::Resource* Buffer;
    BYTE* Cpu;
    UINT Offset;
};

/* Hands out aligned pieces of one mapped upload buffer, replacing the buffer when it runs out */
struct NineUploadRing
{
    NineDevice* Owner;
    UINT Capacity;
    UINT Alignment;
    UINT Cursor;
    TL::unique_comptr<TL::Resource> Buffer;
    BYTE* Mapped;
};

/* Constant buffers each shader stage reads: floats, integers and booleans */
enum NineConstantKind
{
    NineConstantFloat,
    NineConstantInteger,
    NineConstantBoolean,
    NineConstantKindCount
};

/* Texture stages: 16 pixel samplers, the displacement map sampler and 4 vertex samplers */
static constexpr UINT NineStageCount = 21;
static constexpr UINT NineStageStateCount = 35;
static constexpr UINT NineRenderStateCount = 256;
static constexpr UINT NineClipPlaneCount = 8;

/* Registers each constant kind holds, and the bytes one register takes */
static constexpr UINT NineConstantRegisters[NineConstantKindCount] = { 256, 16, 16 };
static constexpr UINT NineConstantStride[NineConstantKindCount] = { 16, 16, 4 };

/* One stage's constants as the application set them, before they are uploaded */
struct NineStageConstants
{
    BYTE Float[256 * 16];
    BYTE Integer[16 * 16];
    BYTE Boolean[16 * 4];
    BOOLEAN Dirty[NineConstantKindCount];
};

struct NineResource;

/* A render target slot: the surface and the translation layer subresource drawn to */
struct NineTargetSlot
{
    NineResource* Resource;
    UINT Sub;
};

static constexpr UINT NineTargetCount = 4;

/* A vertex declaration: its elements and the converter inputs they make */
struct NineVertexDecl
{
    std::vector<NineScElement> Elements;
    NineScInputs Inputs;
    UINT StreamMask;

    /* A POSITIONT element was turned into POSITION, or the declaration has a point size */
    BOOLEAN Pretransformed;
    BOOLEAN PointSize;
};

/* Pipeline stages a shader object belongs to */
enum NineShaderStage
{
    NineVertexStage,
    NinePixelStage,
    NineShaderStageCount
};

/* One conversion of a shader for the pipeline state it was keyed on */
struct NineConverted
{
    std::vector<BYTE> Key;
    NineScBlob Container;
    NineScBlob OutputSignature;
    NineScOutputs Outputs;
    std::vector<D3D12_INPUT_ELEMENT_DESC> Layout;
    std::vector<NineScConstant> Defines[NineScBufferCount];
    UINT Used[NineScBufferCount];
    BYTE PixelOutputs;
    std::unique_ptr<TL::Shader> Shader;
};

/* A Direct3D 9 shader; identical code shares one object that counts its creates */
struct NineShader
{
    NineShaderStage Stage;
    UINT References;
    UINT64 Hash;
    std::vector<BYTE> Code;
    std::vector<std::unique_ptr<NineConverted>> Converted;
};

/* A texture stage: what is bound and the sampler words kept beside it */
struct NineStageTexture
{
    NineResource* Texture;
    BOOLEAN Shadow;
    BOOLEAN SwappedRedBlue;
};

/* One vertex stream: the buffer, where it starts and its stride */
struct NineStreamSlot
{
    NineResource* Buffer;
    UINT Offset;
    UINT Stride;
    const BYTE* User;
    UINT Limit;
};

/* One constant buffer slot fed from its own upload buffer */
struct NineConstantSlot
{
    NineUploadRing Ring;
    NineUploadSlice Current;
    UINT64 Uploaded;
    BOOLEAN Dirty;
};

/* Constant buffer slots the vertex, geometry and pixel extensions use */
static constexpr UINT NineExtensionSlot = 3;
static constexpr UINT NineBumpSlot = 4;
static constexpr UINT NineExtensionSize = 0xA0;

/* Converted shaders the pipeline is built from, with the state they share */
struct NinePipelineKey
{
    NineConverted* Vertex;
    NineConverted* Pixel;
    NineConverted* Geometry;
    D3D12_BLEND_DESC Blend;
    UINT SampleMask;
    D3D12_RASTERIZER_DESC Raster;
    D3D12_DEPTH_STENCIL_DESC Depth;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE Topology;
    UINT TargetCount;
    DXGI_FORMAT TargetFormats[8];
    DXGI_FORMAT DepthFormat;
    DXGI_SAMPLE_DESC Samples;
};

struct NinePipeline
{
    NinePipelineKey Key;
    std::unique_ptr<TL::PipelineState> State;
};

/* State the input assembler and the shader stages changed since the last draw */
static constexpr UINT NineDirtyVertexBuffers = 0x10;
static constexpr UINT NineDirtyIndexBuffer = 0x20;
static constexpr UINT NineDirtyInputLayout = 0x80;
static constexpr UINT NineDirtyPretransformed = 0x2000;

/* The fixed function and pipeline state the DDI sets between draws */
struct NineDeviceState
{
    NineTargetSlot Targets[NineTargetCount];
    UINT TargetMask;
    UINT TargetsUsed;
    NineResource* DepthStencil;

    DWORD RenderStates[NineRenderStateCount];
    DWORD StageStates[NineStageCount][NineStageStateCount];
    NineStageConstants VertexConstants;
    NineStageConstants PixelConstants;
    FLOAT ClipPlanes[NineClipPlaneCount][4];
    BOOLEAN ClipPlanesDirty;
    FLOAT Viewport[4];
    D3DDDIARG_ZRANGE ZRange;
    BOOLEAN ViewportDirty;
    RECT Scissor;
    D3DDDIARG_WINFO WInfo;

    /* The input assembler: streams, their frequencies, the index buffer and the declaration */
    NineStreamSlot Streams[NineScStreamCount];
    UINT StreamsUsed;
    UINT Frequencies[NineScStreamCount];
    NineResource* IndexBuffer;
    UINT IndexStride;
    const BYTE* UserIndices;
    UINT UserIndexLimit;
    NineStageTexture Textures[NineStageCount];
    NineVertexDecl* VertexDecl;
    BOOLEAN Pretransformed;
    UINT Dirty;

    NineShader* Shaders[NineShaderStageCount];

    /* The draw being prepared: primitive, point size and what has to be converted again */
    UINT Primitive;
    FLOAT PointSize[3];
    BOOLEAN PointSizeDirty;
    BOOLEAN PointSizeLarge;
    BOOLEAN VertexKeyDirty;
    BOOLEAN ExtensionDirty;
    BOOLEAN FogDirty;
    BOOLEAN BumpDirty;
    BYTE TextureKinds[16];
    UINT ShadowSamplers;
    NineScRasterStates LastPixelKey;
    BOOLEAN HavePixelKey;

    /* The pixel shader is converted again only when marked, and the blend marking can mark it */
    BOOLEAN PixelDirty;
    BOOLEAN BlendDirty;
    BOOLEAN AlphaToCoverage;
    UINT SwappedTargets;
    UINT PixelAlpha;

    /* The converted shaders and pipeline last drawn with */
    NineConverted* Vertex;
    NineConverted* Geometry;
    NineConverted* Pixel;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc;
    TL::PipelineState* Pipeline;
};

/* A byte range of a buffer locked with discard, kept to turn adjacent discards into no overwrite locks */
struct NineLockRange
{
    UINT Offset;
    UINT Size;
};

/* A surface as the Direct3D 12 present recording takes it */
struct NinePresentPlane
{
    ID3D12Resource* Resource;
    UINT Subresource;
};

/* State Direct3D 12 keeps between recording a present and submitting it */
struct NinePresentRecord
{
    BYTE Opaque[1060];
    UINT Initialized;
    BYTE Reserved[12];
    UINT VidPnSourceId;
};
static_assert(sizeof(NinePresentRecord) == 1080, "Direct3D 12 fills 1080 bytes of present record");

/* Present models the recording takes */
static constexpr UINT NinePresentModelFlip = 2;
static constexpr UINT NinePresentModelBlt = 4;

/* Present recording Direct3D 12 offers on its direct command lists */
struct DECLSPEC_UUID("b0b9e70e-59ce-4415-8116-c8c826b1e599") DECLSPEC_NOVTABLE
INinePresentCommandList : public IUnknown
{
    STDMETHOD_(VOID, Reserved)() = 0;
    STDMETHOD_(VOID, RecordPresent)(
        _In_ ID3D12CommandQueue* Queue,
        _In_reads_(PlaneCount) const NinePresentPlane* Planes,
        _In_ UINT PlaneCount,
        _In_opt_ ID3D12Resource* Destination,
        _In_opt_ VOID* Reserved1,
        _In_ UINT Reserved2,
        _In_ UINT Model,
        _In_ UINT FlipInterval,
        _Out_ BOOL* CommandsAdded,
        _Inout_ NinePresentRecord* Record) = 0;
};

/* Present submission Direct3D 12 offers on its direct queues */
struct DECLSPEC_UUID("c2c09af9-15ce-4c87-bb98-44323032fa7f") DECLSPEC_NOVTABLE
INinePresentQueue : public IUnknown
{
    STDMETHOD(SubmitPresent)(
        _In_ D3DKMT_PRESENT* KmtPresent,
        _In_ NinePresentRecord* Record) = 0;
};

struct NineDevice
{
    NineAdapter* Adapter;

    /* The runtime's own handle and callbacks, kept to call back into d3d9.dll */
    HANDLE RuntimeDevice;
    UINT RuntimeVersion;
    D3DDDI_DEVICECALLBACKS Callbacks;

    ID3D12Device* Device12;
    ID3D12CommandQueue* Queue;

    std::optional<TL::ImmediateContext> Context;
    std::optional<TL::SharedResourceHelpers> Sharing;

    D3D12_FEATURE_DATA_D3D12_OPTIONS19 Options19;

    /* The buffer the zeroed constants live in until a stage sets its own */
    NineUploadRing ConstantRing;
    NineUploadSlice ZeroConstants;
    NineUploadSlice VertexConstants[NineConstantKindCount];
    NineUploadSlice PixelConstants[NineConstantKindCount];

    NineDeviceState State;

    /* Shaders by their code, one map for each stage */
    std::unordered_multimap<UINT64, std::unique_ptr<NineShader>> ShaderCache[NineShaderStageCount];

    /* The converter, the shaders the driver makes itself and every pipeline built so far */
    NineScTranslator* Translator;
    NineShader TransformedVertex;
    NineShader PointGeometry;
    std::vector<std::unique_ptr<NinePipeline>> Pipelines;

    /* Constants of the vertex and pixel stage, then the extension slots */
    NineConstantSlot VertexSlots[NineConstantKindCount];
    NineConstantSlot PixelSlots[NineConstantKindCount];
    NineConstantSlot VertexExtension;
    NineConstantSlot GeometryExtension;
    NineConstantSlot PixelFog;
    NineConstantSlot PixelBump;
    BYTE Extension[NineExtensionSize];
    BYTE Bump[256];

    /* Samplers by their state words, user memory vertices and the triangle fan indices */
    std::vector<std::pair<std::array<UINT, 4>, std::unique_ptr<TL::Sampler>>> Samplers;
    NineUploadRing UserRing;
    NineUploadRing FanRing;
    UINT FanCapacity;
    TL::unique_comptr<TL::Resource, NineUnderlyingDeleter> UserFanList;

    /* Discarded ranges of each buffer locked since it was created */
    std::mutex LockRangesLock;
    std::unordered_map<NineResource*, std::vector<NineLockRange>> LockRanges;

    /* Objects the runtime registered as its own, which share without reopening */
    std::mutex WrappedLock;
    std::set<IUnknown*> Wrapped;

    /* The present the runtime's present callback comes back for, and what recording it needs */
    const D3DDDIARG_PRESENT1* PendingPresent;
    TL::MaxFrameLatencyHelper Latency;
    NinePresentRecord Record;
    TL::unique_comptr<INinePresentQueue> PresentQueue;
};

/* What d3d9.dll passes beside an open of a shared resource */
struct NineOpenResourceArgs
{
    D3DDDIFORMAT Format;
};

/* What d3d9.dll learns about a shared resource before opening it */
struct NineSharedResourceInfo
{
    UINT Width;
    UINT Height;
    D3DDDIFORMAT Format;
    D3DRESOURCETYPE Type;
    UINT64 Usage;
    BOOL NtHandle;
    BOOL CreatedBy9On12;

    /* Textures give their multisample type and quality, buffers their size in the second */
    UINT Multisample;
    UINT QualityOrSize;
    UINT Levels;
    UINT Depth;
};

/* What one lock of a subresource asked for, kept until its unlock */
struct NineLockState
{
    UINT Flags;
    D3D12_BOX Box;
    UINT Count;
};

/* Where the runtime keeps the initial contents of one surface */
struct NineSysMemSource
{
    const BYTE* Data;
    UINT64 RowPitch;
    UINT64 SlicePitch;
};

/* A Direct3D 9 surface, texture or buffer and the translation layer resource behind it */
struct NineResource
{
    NineDevice* Device;
    HANDLE RuntimeResource;
    UINT64 UniqueId;

    D3DDDIFORMAT Format;
    DXGI_FORMAT DxgiFormat;
    DXGI_FORMAT DepthViewFormat;
    UINT VidPnSourceId;

    /* An X8R8G8B8 primary is created as A8R8G8B8 and keeps reading alpha as one */
    BOOLEAN PromotedPrimary;
    BOOLEAN Lockable;
    BOOLEAN AutogenMips;
    BOOLEAN CubeMap;
    BOOLEAN SystemMemory;
    BOOLEAN DecodeBuffer;
    BOOLEAN SampledDepth;

    /* Reset by every lock that writes */
    UINT GpuWrites;
    BOOLEAN CpuDirty;

    UINT8 ArraySize;
    UINT8 PlaneCount;
    UINT Subresources;
    UINT CpuAccess;
    UINT64 TotalSize;

    /* The description as the application asked for it, and as Direct3D 12 creates it */
    D3D12_RESOURCE_DESC Typed;
    D3D12_RESOURCE_DESC Desc;

    std::vector<NineLockState> Locks;
    std::vector<NineSysMemSource> SysMem;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> Footprints;
    std::vector<UINT64> RowSizes;
    std::vector<UINT> RowCounts;

    NineUnderlying Underlying;
    std::vector<std::unique_ptr<TL::TRTV>> Rtvs;
    std::vector<std::unique_ptr<TL::TRTV>> SrgbRtvs;
    std::unique_ptr<TL::TDSV> Dsvs[4];
    std::unique_ptr<TL::TSRV> Srv;
    std::unique_ptr<TL::TSRV> SrgbSrv;
    std::unique_ptr<TL::TSRV> TopMipSrv[2];
    UINT TextureType;

    /* Render target slots and the depth slot this resource is bound to */
    std::vector<UINT> TargetSlots;
    BOOLEAN BoundAsDepth;

    /* Vertex streams this buffer is bound to, and whether it is the index buffer */
    std::vector<UINT> StreamSlots;
    BOOLEAN BoundAsIndices;

    /* The list indices last made from this index buffer for a triangle fan, and where they started */
    TL::unique_comptr<TL::Resource, NineUnderlyingDeleter> FanList;
    UINT FanStart;
    BOOLEAN FanValid;

    /* The renamed allocation an asynchronous discard lock handed out */
    TL::D3D12ResourceSuballocation Renamed;
};

/* resource.cpp */
HRESULT
APIENTRY
NineCreateResource2(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATERESOURCE2* Args);

HRESULT
APIENTRY
NineDestroyResource(
    _In_ HANDLE hDevice,
    _In_ HANDLE hResource);

HRESULT
APIENTRY
NineOpenResource(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_OPENRESOURCE* Args,
    _In_ const NineOpenResourceArgs* OpenArgs);

HRESULT
APIENTRY
NineLock(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_LOCK* Args);

HRESULT
APIENTRY
NineUnlock(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_UNLOCK* Args);

HRESULT
APIENTRY
NineLockAsync(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_LOCKASYNC* Args);

HRESULT
APIENTRY
NineUnlockAsync(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_UNLOCKASYNC* Args);

/* The translation layer subresource of a runtime subresource index and plane */
UINT
NineSubresourceOf(
    _In_ const NineResource* Resource,
    _In_ UINT Index,
    _In_ UINT Plane,
    _In_ BOOLEAN Expanded);

/* Swaps in the allocation an asynchronous discard lock wrote to */
HRESULT
APIENTRY
NineRename(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_RENAME* Args);

/* The shader view of a kind: bit 0 reads through sRGB, bit 1 shows the top mip only */
TL::TSRV*
NineShaderViewOf(
    _Inout_ NineResource* Resource,
    _In_ UINT Kind);

/* output.cpp */
HRESULT
APIENTRY
NineSetRenderTarget(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETRENDERTARGET* Args);

HRESULT
APIENTRY
NineSetDepthStencil(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETDEPTHSTENCIL* Args);

HRESULT
APIENTRY
NineClear(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_CLEAR* Args,
    _In_ UINT NumRects,
    _In_reads_opt_(NumRects) const RECT* Rects);

HRESULT
APIENTRY
NineColorFill(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_COLORFILL* Args);

/* blit.cpp */
HRESULT
APIENTRY
NineBlt(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_BLT* Args);

HRESULT
APIENTRY
NineTexBlt1(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_TEXBLT1* Args);

HRESULT
APIENTRY
NineVolBlt1(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_VOLUMEBLT1* Args);

HRESULT
APIENTRY
NineBufBlt1(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_BUFFERBLT1* Args);

/* A copy of a resource the CPU cannot read, in a staging resource it can */
TL::unique_comptr<TL::Resource>
NineStagingCopy(
    _In_ NineResource* Resource);

/* input.cpp */
HRESULT
APIENTRY
NineCreateVertexShaderDecl(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATEVERTEXSHADERDECL* Args,
    _In_reads_(Args->NumVertexElements) const D3DDDIVERTEXELEMENT* Elements);

HRESULT
APIENTRY
NineSetVertexShaderDecl(
    _In_ HANDLE hDevice,
    _In_ HANDLE hDecl);

HRESULT
APIENTRY
NineDeleteVertexShaderDecl(
    _In_ HANDLE hDevice,
    _In_ HANDLE hDecl);

HRESULT
APIENTRY
NineSetStreamSource(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETSTREAMSOURCE* Args);

HRESULT
APIENTRY
NineSetStreamSourceFreq(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETSTREAMSOURCEFREQ* Args);

HRESULT
APIENTRY
NineSetIndices(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETINDICES* Args);

/* Takes a buffer that is going away off every stream and the index buffer */
VOID
NineUnbindInputs(
    _Inout_ NineResource* Resource);

/* shader.cpp */
HRESULT
APIENTRY
NineCreateVertexShaderFunc(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATEVERTEXSHADERFUNC* Args,
    _In_ const UINT* Code);

HRESULT
APIENTRY
NineDeleteVertexShaderFunc(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader);

HRESULT
APIENTRY
NineSetVertexShaderFunc(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader);

HRESULT
APIENTRY
NineCreatePixelShader(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATEPIXELSHADER* Args,
    _In_ const UINT* Code);

HRESULT
APIENTRY
NineDeletePixelShader(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader);

HRESULT
APIENTRY
NineSetPixelShader(
    _In_ HANDLE hDevice,
    _In_ HANDLE hShader);

VOID
NineUnbindOutputs(
    _Inout_ NineResource* Resource);

/* query.cpp */
HRESULT
APIENTRY
NineCreateQuery(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATEQUERY* Args);

HRESULT
APIENTRY
NineDestroyQuery(
    _In_ HANDLE hDevice,
    _In_ HANDLE hQuery);

HRESULT
APIENTRY
NineIssueQuery(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_ISSUEQUERY* Args);

HRESULT
APIENTRY
NineGetQueryData(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_GETQUERYDATA* Args);

/* interop.cpp */
VOID
APIENTRY
NineAcquireResource(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SYNCTOKEN* Args);

VOID
APIENTRY
NineReleaseResource(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SYNCTOKEN* Args);

HRESULT
APIENTRY
NineGetD3D12Device(
    _In_ HANDLE hDevice,
    _In_ REFIID Riid,
    _COM_Outptr_ VOID** Object);

HRESULT
APIENTRY
NineGetD3D12Resource(
    _In_ HANDLE hResource,
    _In_ REFIID Riid,
    _COM_Outptr_ VOID** Object);

VOID
APIENTRY
NineTransitionResource(
    _In_ HANDLE hDevice,
    _In_ HANDLE hResource,
    _In_ D3D12_RESOURCE_STATES State);

VOID
APIENTRY
NineSetResourceState(
    _In_ HANDLE hDevice,
    _In_ HANDLE hResource,
    _In_ D3D12_RESOURCE_STATES State);

VOID
APIENTRY
NineSetMaximumFrameLatency(
    _In_ HANDLE hDevice,
    _In_ UINT MaxFrameLatency);

HRESULT
APIENTRY
NineAddResourceWaitsToQueue(
    _In_ HANDLE hResource,
    _In_ ID3D12CommandQueue* Queue);

HRESULT
APIENTRY
NineAddDeferredWaitsToResource(
    _In_ HANDLE hResource,
    _In_ UINT NumSync,
    _In_reads_(NumSync) UINT64* SignalValues,
    _In_reads_(NumSync) ID3D12Fence** Fences);

/* present.cpp */
HRESULT
APIENTRY
NinePresent1(
    _In_ HANDLE hDevice,
    _In_ D3DDDIARG_PRESENT1* Args);

HRESULT
APIENTRY
NineKernelPresent(
    _In_ HANDLE hDevice,
    _Inout_ D3DKMT_PRESENT* KmtPresent);

/* share.cpp */
HRESULT
APIENTRY
NineGetSharedGdiHandle(
    _In_ HANDLE hDevice,
    _In_ HANDLE hResource,
    _Out_ HANDLE* Shared);

HRESULT
APIENTRY
NineCreateSharedNtHandle(
    _In_ HANDLE hDevice,
    _In_ HANDLE hResource,
    _In_opt_ SECURITY_ATTRIBUTES* Attributes,
    _Out_ HANDLE* Shared);

UINT
APIENTRY
NineQueryResourceDriverDataSize(
    _In_ HANDLE hDevice);

HRESULT
APIENTRY
NineCreateKmtHandle(
    _In_ HANDLE hDevice,
    _In_ HANDLE Object,
    _Out_ UINT* Kmt,
    _Out_writes_bytes_(DataSize) VOID* Data,
    _In_ UINT DataSize);

HRESULT
APIENTRY
NineQueryResourceFromKmtHandle(
    _In_ HANDLE hDevice,
    _In_ UINT Kmt,
    _Out_ NineSharedResourceInfo* Info);

HRESULT
APIENTRY
NineDestroyKmtHandle(
    _In_ HANDLE hDevice,
    _In_ UINT Kmt);

HRESULT
APIENTRY
NineCreateWrappingHandle(
    _In_ HANDLE hDevice,
    _In_ IUnknown* Object,
    _Out_ HANDLE* Wrapping);

VOID
APIENTRY
NineDestroyWrappingHandle(
    _In_ HANDLE hDevice,
    _In_ HANDLE Wrapping);

/* device.cpp */
HRESULT
APIENTRY
NineCreateDevice(
    _In_ HANDLE hAdapter,
    _Inout_ D3DDDIARG_CREATEDEVICE* Args);

HRESULT
APIENTRY
NineDestroyDevice(
    _In_ HANDLE hDevice);

/* ddi.cpp */
VOID
NineFillDeviceFuncs(
    _Out_ D3DDDI_DEVICEFUNCS* Functions);

/* device.cpp */
NineUploadSlice
NineAllocateUpload(
    _Inout_ NineUploadRing* Ring,
    _In_ UINT Size);

/* draw.cpp */
HRESULT
APIENTRY
NineSetTexture(
    _In_ HANDLE hDevice,
    _In_ UINT Stage,
    _In_ HANDLE hTexture);

HRESULT
APIENTRY
NineSetStreamSourceUm(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETSTREAMSOURCEUM* Args,
    _In_ const VOID* Data);

HRESULT
APIENTRY
NineSetIndicesUm(
    _In_ HANDLE hDevice,
    _In_ UINT IndexSize,
    _In_ const VOID* Data);

HRESULT
APIENTRY
NineDrawPrimitive(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_DRAWPRIMITIVE* Args,
    _In_opt_ const UINT* FlagBuffer);

HRESULT
APIENTRY
NineDrawPrimitive2(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_DRAWPRIMITIVE2* Args);

HRESULT
APIENTRY
NineDrawIndexedPrimitive(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE* Args);

HRESULT
APIENTRY
NineDrawIndexedPrimitive2(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE2* Args,
    _In_ UINT IndicesSize,
    _In_ const VOID* Indices,
    _In_opt_ const UINT* FlagBuffer);

/* Sets up the constant slots of a new device */
VOID
NineInitDrawState(
    _Inout_ NineDevice* Device);

/* Lets go of every converted shader of a shader and the pipelines built from them */
VOID
NineDropConverted(
    _Inout_ NineDevice* Device,
    _Inout_ NineShader* Shader);

/* Frees the converter and every pipeline before the context goes */
VOID
NineFreeDrawState(
    _Inout_ NineDevice* Device);

/* Marks the pixel shader for conversion when a state it is keyed on changed since the last look */
VOID
NineNotePixelKey(
    _Inout_ NineDevice* Device);

/* state.cpp */
HRESULT
APIENTRY
NineSetRenderState(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_RENDERSTATE* Args);

HRESULT
APIENTRY
NineUpdateWInfo(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_WINFO* Args);

HRESULT
APIENTRY
NineSetTextureStageState(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_TEXTURESTAGESTATE* Args);

HRESULT
APIENTRY
NineSetPixelShaderConst(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONST* Args,
    _In_ const FLOAT* Values);

HRESULT
APIENTRY
NineSetPixelShaderConstI(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONSTI* Args,
    _In_ const INT* Values);

HRESULT
APIENTRY
NineSetPixelShaderConstB(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETPIXELSHADERCONSTB* Args,
    _In_ const BOOL* Values);

HRESULT
APIENTRY
NineSetVertexShaderConst(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONST* Args,
    _In_ const VOID* Values);

HRESULT
APIENTRY
NineSetVertexShaderConstI(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONSTI* Args,
    _In_ const INT* Values);

HRESULT
APIENTRY
NineSetVertexShaderConstB(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETVERTEXSHADERCONSTB* Args,
    _In_ const BOOL* Values);

HRESULT
APIENTRY
NineSetViewport(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_VIEWPORTINFO* Args);

HRESULT
APIENTRY
NineSetZRange(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_ZRANGE* Args);

HRESULT
APIENTRY
NineSetScissorRect(
    _In_ HANDLE hDevice,
    _In_ const RECT* Rect);

HRESULT
APIENTRY
NineSetClipPlane(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETCLIPPLANE* Args);

HRESULT
APIENTRY
NineFlush1(
    _In_ HANDLE hDevice,
    _In_ UINT FlushFlags);
