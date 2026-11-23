/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Driver call batching on a worker thread
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Bytes one recording buffer holds */
static constexpr UINT D9BatchCapacity = 0xC0000;

/* An idle worker gets the buffer once it holds this many calls */
static constexpr UINT D9BatchHandOff = 80;

/* Sizes of the system memory fills each buffer keeps locked */
static constexpr UINT D9FillVertexBytes = 0x8000;
static constexpr UINT D9FillIndexBytes = 0x4000;

/* Resource flags of the fills: dynamic, write only vertex or index buffers */
static constexpr UINT D9FillVertexFlags = 0x00080044;
static constexpr UINT D9FillIndexFlags = 0x00100044;

/* The header in front of every recorded call */
struct D9BatchRecord
{
    D9BatchCall Call;
    UINT Size;
};

static_assert(sizeof(D9BatchRecord) == 8);

/* A recorded constant call: the register range, then the data */
struct D9ConstantRecord
{
    UINT Register;
    UINT Count;
};

/* Makes a recorded constant call with the copy of the data that follows it */
static
VOID
D9RunShaderConstants(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ HANDLE Driver,
    _In_ D9BatchCall Call,
    _In_ BYTE* Args)
{
    D9ConstantRecord* Record = reinterpret_cast<D9ConstantRecord*>(Args);
    VOID* Data = Record + 1;
    D3DDDIARG_SETVERTEXSHADERCONST Range = {};

    Range.Register = Record->Register;
    Range.Count = Record->Count;
    switch (Call)
    {
    case D9BatchCall::VertexShaderFunc:
        Funcs->pfnSetVertexShaderFunc(Driver, *static_cast<HANDLE*>(Data));
        break;
    case D9BatchCall::VertexShaderConst:
        Funcs->pfnSetVertexShaderConst(Driver, &Range, Data);
        break;
    case D9BatchCall::VertexShaderConstI:
        Funcs->pfnSetVertexShaderConstI(Driver, reinterpret_cast<D3DDDIARG_SETVERTEXSHADERCONSTI*>(&Range),
                                        static_cast<INT*>(Data));
        break;
    case D9BatchCall::VertexShaderConstB:
        Funcs->pfnSetVertexShaderConstB(Driver, reinterpret_cast<D3DDDIARG_SETVERTEXSHADERCONSTB*>(&Range),
                                        static_cast<BOOL*>(Data));
        break;
    case D9BatchCall::PixelShaderConstI:
        Funcs->pfnSetPixelShaderConstI(Driver, reinterpret_cast<D3DDDIARG_SETPIXELSHADERCONSTI*>(&Range),
                                       static_cast<INT*>(Data));
        break;
    case D9BatchCall::PixelShaderConstB:
        Funcs->pfnSetPixelShaderConstB(Driver, reinterpret_cast<D3DDDIARG_SETPIXELSHADERCONSTB*>(&Range),
                                       static_cast<BOOL*>(Data));
        break;
    default:
        break;
    }
}

HRESULT
D9RunBufBlt(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ HANDLE Driver,
    _In_ const D3DDDIARG_BUFFERBLT1* Blt)
{
    D3DDDIARG_BUFFERBLT Old = {};

    if (Funcs->pfnBufBlt1 != nullptr)
        return Funcs->pfnBufBlt1(Driver, Blt);
    Old.hDstResource = Blt->hDstResource;
    Old.hSrcResource = Blt->hSrcResource;
    Old.Offset = Blt->Offset;
    Old.SrcRange = Blt->SrcRange;
    return Funcs->pfnBufBlt(Driver, &Old);
}

HRESULT
D9RunTexBlt(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ HANDLE Driver,
    _In_ const D3DDDIARG_TEXBLT1* Blt)
{
    D3DDDIARG_TEXBLT Old = {};

    if (Funcs->pfnTexBlt1 != nullptr)
        return Funcs->pfnTexBlt1(Driver, Blt);
    Old.hDstResource = Blt->hDstResource;
    Old.hSrcResource = Blt->hSrcResource;
    Old.CubeMapFace = Blt->CubeMapFace;
    Old.DstPoint = Blt->DstPoint;
    Old.SrcRect = Blt->SrcRect;
    return Funcs->pfnTexBlt(Driver, &Old);
}

HRESULT
D9RunVolBlt(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ HANDLE Driver,
    _In_ const D3DDDIARG_VOLUMEBLT1* Blt)
{
    D3DDDIARG_VOLUMEBLT Old = {};

    if (Funcs->pfnVolBlt1 != nullptr)
        return Funcs->pfnVolBlt1(Driver, Blt);
    Old.hDstResource = Blt->hDstResource;
    Old.hSrcResource = Blt->hSrcResource;
    Old.DstX = Blt->DstX;
    Old.DstY = Blt->DstY;
    Old.DstZ = Blt->DstZ;
    Old.SrcBox = Blt->SrcBox;
    return Funcs->pfnVolBlt(Driver, &Old);
}

D9Batch::D9Batch(
    _In_ D9Device* Owner,
    _In_ const D3DDDI_DEVICEFUNCS* Functions,
    _In_ HANDLE DriverDevice)
    : Device(Owner),
      Funcs(Functions),
      Driver(DriverDevice)
{
}

D9Batch::~D9Batch()
{
    Stop();
}

/* A fill is a dynamic buffer placed like any other; in system memory the runtime owns its memory */
HRESULT
D9Batch::CreateFills(
    _Inout_ D9BatchBuffer* Buffer)
{
    static const struct
    {
        D3DDDIFORMAT Format;
        UINT Size;
        UINT Flags;
    } Fills[3] =
    {
        { D3DDDIFMT_VERTEXDATA, D9FillVertexBytes, D9FillVertexFlags },
        { D3DDDIFMT_INDEX16, D9FillIndexBytes, D9FillIndexFlags },
        { D3DDDIFMT_INDEX32, D9FillIndexBytes, D9FillIndexFlags },
    };
    HRESULT hr;

    for (UINT Index = 0; Index < _countof(Fills); Index++)
    {
        D9BatchFill* Fill = &Buffer->Fills[Index];
        D3DDDI_POOL Placement = Device->BufferPlacement(Index == 0, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                                        D3DPOOL_DEFAULT);

        Fill->Size = Fills[Index].Size;
        if (Placement != D3DDDIPOOL_SYSTEMMEM)
        {
            hr = Device->CreateBufferResource(static_cast<D3DFORMAT>(Fills[Index].Format), Fills[Index].Size,
                                              Fills[Index].Flags, 0, Placement, nullptr, nullptr, &Fill->Resource);
            if (FAILED(hr))
                return hr;
            continue;
        }

        Fill->Memory = VirtualAlloc(nullptr, Fills[Index].Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (Fill->Memory == nullptr)
            return E_OUTOFMEMORY;

        hr = Device->CreateSystemBuffer(Fills[Index].Format, Fills[Index].Size, Fills[Index].Flags, Fill->Memory,
                                        &Fill->Resource);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* The fills stay locked while the main thread records into their buffer; one still locked keeps what it holds,
   and system memory ones only tell the driver */
VOID
D9Batch::LockFills(
    _Inout_ D9BatchBuffer* Buffer)
{
    for (D9BatchFill& Fill : Buffer->Fills)
    {
        D3DDDIARG_LOCK Lock = {};

        if (Fill.Data != nullptr || Fill.Resource.DriverResource == NULL)
            continue;
        Fill.Used = 0;
        Lock.hResource = Fill.Resource.DriverResource;
        Lock.Flags.Discard = TRUE;
        Lock.Flags.NotifyOnly = Fill.Resource.Pool == D3DDDIPOOL_SYSTEMMEM;
        if (FAILED(Funcs->pfnLock(Driver, &Lock)))
        {
            D9_TRACE_ONCE(L"the driver would not lock a batch fill");
            return;
        }
        Fill.Data = static_cast<BYTE*>(Fill.Memory != nullptr ? Fill.Memory : Lock.pSurfData);
    }
}

/* Fills that took a draw's data are unlocked before the driver reads them */
VOID
D9Batch::UnlockFills(
    _Inout_ D9BatchBuffer* Buffer)
{
    for (D9BatchFill& Fill : Buffer->Fills)
    {
        D3DDDIARG_UNLOCK Unlock = {};

        if (Fill.Used == 0 || Fill.Resource.DriverResource == NULL)
            continue;
        Fill.Used = 0;
        Fill.Data = nullptr;
        Unlock.hResource = Fill.Resource.DriverResource;
        Unlock.Flags.NotifyOnly = Fill.Resource.Pool == D3DDDIPOOL_SYSTEMMEM;
        Funcs->pfnUnlock(Driver, &Unlock);
    }
}

VOID
D9Batch::DestroyFills(
    _Inout_ D9BatchBuffer* Buffer)
{
    /* Locked index fills in video memory are unlocked before they go; the vertex fill goes still locked */
    for (UINT Index = 0; Index < D9FillCount; Index++)
    {
        D9BatchFill* Fill = &Buffer->Fills[Index];

        if (Index != D9FillVertices && Fill->Resource.DriverResource != NULL && Fill->Data != nullptr &&
            Fill->Resource.Pool != D3DDDIPOOL_SYSTEMMEM)
        {
            D3DDDIARG_UNLOCK Unlock = {};

            Unlock.hResource = Fill->Resource.DriverResource;
            Funcs->pfnUnlock(Driver, &Unlock);
        }
        if (Fill->Resource.DriverResource != NULL)
            Funcs->pfnDestroyResource(Driver, Fill->Resource.DriverResource);
        Fill->Resource.DriverResource = NULL;
        Fill->Data = nullptr;
        Fill->Used = 0;
        if (Fill->Memory != nullptr)
            VirtualFree(Fill->Memory, 0, MEM_RELEASE);
        Fill->Memory = nullptr;
    }
}

HRESULT
D9Batch::Start(VOID)
{
    HRESULT hr;

    for (D9BatchBuffer& Buffer : Buffers)
    {
        Buffer.Data = static_cast<BYTE*>(VirtualAlloc(nullptr, D9BatchCapacity, MEM_COMMIT | MEM_RESERVE,
                                                      PAGE_READWRITE));
        if (Buffer.Data == nullptr)
            return E_OUTOFMEMORY;
    }
    for (D9BatchBuffer& Buffer : Buffers)
    {
        hr = CreateFills(&Buffer);
        if (FAILED(hr))
            return hr;
    }

    /* The main thread records into the second buffer first, locked before the idle one */
    LockFills(&Buffers[1]);
    LockFills(&Buffers[0]);

    Wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (Wake == NULL || Done == NULL)
        return E_OUTOFMEMORY;
    Thread = CreateThread(nullptr, 0, WorkerMain, this, 0, nullptr);
    if (Thread == NULL)
    {
        D9Trace(L"the batch worker thread could not start (%lu)", GetLastError());
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

/* The fills go first; what is still recorded then runs on the calling thread */
VOID
D9Batch::Stop(VOID)
{
    WaitForWorker();
    for (D9BatchBuffer& Buffer : Buffers)
        DestroyFills(&Buffer);
    if (Buffers[Current].Data != nullptr)
        Drain();

    if (Thread != NULL)
    {
        Exiting = TRUE;
        SetEvent(Wake);
        WaitForSingleObject(Thread, INFINITE);
        CloseHandle(Thread);
        Thread = NULL;
    }
    if (Wake != NULL)
        CloseHandle(Wake);
    if (Done != NULL)
        CloseHandle(Done);
    Wake = NULL;
    Done = NULL;

    for (D9BatchBuffer& Buffer : Buffers)
    {
        if (Buffer.Data != nullptr)
            VirtualFree(Buffer.Data, 0, MEM_RELEASE);
        Buffer.Data = nullptr;
    }
}

DWORD
WINAPI
D9Batch::WorkerMain(
    _In_ VOID* Context)
{
    D9Batch* Batch = static_cast<D9Batch*>(Context);

    for (;;)
    {
        WaitForSingleObject(Batch->Wake, INFINITE);
        if (Batch->Exiting)
            return 0;

        Batch->Run(&Batch->Buffers[Batch->Current ^ 1]);
        InterlockedExchange(&Batch->Busy, 0);
        SetEvent(Batch->Done);
    }
}

VOID
D9Batch::WaitForWorker(VOID)
{
    while (Pending)
    {
        if (WaitForSingleObject(Done, 1000) == WAIT_OBJECT_0)
            Pending = FALSE;
    }
}

/* Room for one call; a full buffer, or one an idle worker can take, is handed over first */
_Ret_maybenull_
VOID*
D9Batch::Record(
    _In_ D9BatchCall Call,
    _In_ UINT Size)
{
    D9BatchBuffer* Buffer = &Buffers[Current];
    UINT Needed = (sizeof(D9BatchRecord) + Size + 7) & ~7u;
    D9BatchRecord* Header;

    if (Needed > D9BatchCapacity)
        return nullptr;
    if (D9BatchCapacity - Buffer->Used < Needed ||
        (ReadAcquire(&Busy) == 0 && Buffer->Calls >= D9BatchHandOff))
    {
        Submit();
        Buffer = &Buffers[Current];
    }

    Header = reinterpret_cast<D9BatchRecord*>(Buffer->Data + Buffer->Used);
    Header->Call = Call;
    Header->Size = Needed;
    Buffer->Used += Needed;
    Buffer->Calls += 1;
    return Header + 1;
}

/* Hands the recording buffer to the worker and records into the other one, whose fills lock again */
VOID
D9Batch::Submit(VOID)
{
    WaitForWorker();
    UnlockFills(&Buffers[Current]);
    Current ^= 1;
    LockFills(&Buffers[Current]);
    Buffers[Current].Used = 0;
    Buffers[Current].Calls = 0;
    InterlockedExchange(&Busy, 1);
    Pending = TRUE;
    SetEvent(Wake);
    Advance();
}

/* Waits for the worker, then runs what is recorded on the calling thread with the fills it used unlocked */
VOID
D9Batch::Drain(VOID)
{
    WaitForWorker();
    UnlockFills(&Buffers[Current]);
    Run(&Buffers[Current]);
    LockFills(&Buffers[Current]);
    Advance();
}

/* The vertices a primitive count of a type reads; an unknown type reads none */
static
UINT
D9PrimitiveVertices(
    _In_ D3DPRIMITIVETYPE Type,
    _In_ UINT Count)
{
    switch (Type)
    {
    case D3DPT_POINTLIST:
        return Count;
    case D3DPT_LINELIST:
        return Count * 2;
    case D3DPT_LINESTRIP:
        return Count + 1;
    case D3DPT_TRIANGLELIST:
        return Count * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        return Count + 2;
    default:
        return 0;
    }
}

/* Draws reading more than this many vertices or indices read application memory where it lies */
static constexpr UINT D9FillDrawLimit = 0x100;

VOID
D9Batch::RecordMemory(
    _In_ D9BatchCall Call,
    _In_ UINT Stride,
    _In_ const VOID* Data)
{
    D9MemoryRecord* Record = static_cast<D9MemoryRecord*>(this->Record(Call, sizeof(*Record)));

    if (Record == nullptr)
        return;
    Record->Stride = Stride;
    Record->Data = Data;
}

/* A small draw from application memory copies its vertices into the vertex fill; a large one runs at once */
VOID
D9Batch::DrawMemory(
    _In_ const D3DDDIARG_DRAWPRIMITIVE* Draw,
    _In_ UINT Stride,
    _In_ const VOID* Vertices)
{
    UINT Count = D9PrimitiveVertices(Draw->PrimitiveType, Draw->PrimitiveCount);
    UINT Size = Count * Stride;
    D9FillDrawRecord* Record;
    D9BatchFill* Fill;
    VOID* Slot;

    Bound[D9BoundStreams] = NULL;
    if (Count > D9FillDrawLimit)
    {
        RecordMemory(D9BatchCall::StreamSourceUm, Stride, Vertices);
        Slot = this->Record(D9BatchCall::DrawPrimitive, sizeof(*Draw));
        if (Slot != nullptr)
            CopyMemory(Slot, Draw, sizeof(*Draw));
        Drain();
        return;
    }

    if (Buffers[Current].Fills[D9FillVertices].Used + Size > Buffers[Current].Fills[D9FillVertices].Size)
        Submit();
    Record = static_cast<D9FillDrawRecord*>(this->Record(D9BatchCall::DrawFill, sizeof(*Record)));
    Fill = &Buffers[Current].Fills[D9FillVertices];
    if (Record == nullptr || Fill->Data == nullptr)
        return;

    ZeroMemory(Record, sizeof(*Record));
    Record->Stream.hVertexBuffer = Fill->Resource.DriverResource;
    Record->Stream.Stride = Stride;
    Record->Draw.PrimitiveType = Draw->PrimitiveType;
    Record->Draw.FirstVertexOffset = Fill->Used;
    Record->Draw.PrimitiveCount = Draw->PrimitiveCount;
    CopyMemory(Fill->Data + Fill->Used, Vertices, Size);
    Fill->Used += Size;
}

/* A small indexed draw copies the vertices it names into the vertex fill, starting at a whole vertex, and its
   indices, moved down to the first of them, into the index fill of their size */
VOID
D9Batch::DrawIndexedMemory(
    _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE* Draw,
    _In_ UINT Stride,
    _In_ const VOID* Vertices,
    _In_ UINT IndexStride,
    _In_ const VOID* IndexData)
{
    UINT Indices = D9PrimitiveVertices(Draw->PrimitiveType, Draw->PrimitiveCount);
    UINT IndexBytes = Indices * IndexStride;
    UINT VertexBytes = Draw->NumVertices * Stride;
    UINT Which = IndexStride == 2 ? D9FillIndices16 : D9FillIndices32;
    D9BatchFill* Fill = &Buffers[Current].Fills[D9FillVertices];
    D9BatchFill* IndexFill;
    D9FillIndexedRecord* Record;
    BYTE* Target;
    VOID* Slot;

    Bound[D9BoundStreams] = NULL;
    Bound[D9BoundIndices] = NULL;
    if (Draw->NumVertices > D9FillDrawLimit || Indices > D9FillDrawLimit)
    {
        RecordMemory(D9BatchCall::StreamSourceUm, Stride, Vertices);
        RecordMemory(D9BatchCall::IndicesUm, IndexStride, IndexData);
        Slot = this->Record(D9BatchCall::DrawIndexedPrimitive, sizeof(*Draw));
        if (Slot != nullptr)
            CopyMemory(Slot, Draw, sizeof(*Draw));
        Drain();
        return;
    }

    if (Fill->Used % Stride != 0)
        Fill->Used = (Fill->Used / Stride + 1) * Stride;
    IndexFill = &Buffers[Current].Fills[Which];
    if (Fill->Used + VertexBytes > Fill->Size || IndexFill->Used + IndexBytes > IndexFill->Size)
        Submit();
    Record = static_cast<D9FillIndexedRecord*>(this->Record(D9BatchCall::DrawIndexedFill, sizeof(*Record)));
    Fill = &Buffers[Current].Fills[D9FillVertices];
    IndexFill = &Buffers[Current].Fills[Which];
    if (Record == nullptr || Fill->Data == nullptr || IndexFill->Data == nullptr)
        return;

    ZeroMemory(Record, sizeof(*Record));
    Record->Stream.hVertexBuffer = Fill->Resource.DriverResource;
    Record->Stream.Stride = Stride;
    Record->Indices.hIndexBuffer = IndexFill->Resource.DriverResource;
    Record->Indices.Stride = IndexStride;
    Record->Draw.PrimitiveType = Draw->PrimitiveType;
    Record->Draw.BaseVertexIndex = static_cast<INT>(Fill->Used / Stride);
    Record->Draw.NumVertices = Draw->NumVertices;
    Record->Draw.StartIndex = IndexFill->Used / IndexStride;
    Record->Draw.PrimitiveCount = Draw->PrimitiveCount;
    CopyMemory(Fill->Data + Fill->Used, static_cast<const BYTE*>(Vertices) + Stride * Draw->MinIndex, VertexBytes);
    Fill->Used += VertexBytes;

    Target = IndexFill->Data + IndexFill->Used;
    if (Draw->MinIndex == 0)
    {
        CopyMemory(Target, IndexData, IndexBytes);
    }
    else if (IndexStride == 2)
    {
        for (UINT Index = 0; Index < Indices; Index++)
        {
            reinterpret_cast<WORD*>(Target)[Index] =
                static_cast<WORD>(static_cast<const WORD*>(IndexData)[Index] - Draw->MinIndex);
        }
    }
    else
    {
        for (UINT Index = 0; Index < Indices; Index++)
            reinterpret_cast<DWORD*>(Target)[Index] = static_cast<const DWORD*>(IndexData)[Index] - Draw->MinIndex;
    }
    IndexFill->Used += IndexBytes;
}

/* A new generation starts with everything bound already in use */
VOID
D9Batch::Advance(VOID)
{
    Generation += 1;
    for (HANDLE Resource : Bound)
        Reference(Resource);
}

VOID
D9Batch::Reference(
    _In_opt_ HANDLE Resource)
{
    if (Resource != NULL)
        References[reinterpret_cast<ULONG_PTR>(Resource) % D9BatchReferenceSlots] = static_cast<BYTE>(Generation);
}

/* Used by the generation being recorded, or by the one the worker may still be running */
BOOLEAN
D9Batch::IsReferenced(
    _In_ HANDLE Resource) const
{
    BYTE Last = References[reinterpret_cast<ULONG_PTR>(Resource) % D9BatchReferenceSlots];

    if (Last == static_cast<BYTE>(Generation))
        return TRUE;
    return Pending && Last == static_cast<BYTE>(Generation - 1);
}

/* Before the driver touches a resource itself, a batch that uses it runs; otherwise only the worker is waited on */
VOID
D9Batch::Settle(
    _In_ HANDLE Resource)
{
    if (IsReferenced(Resource))
        Drain();
    else
        WaitForWorker();
}

VOID
D9Batch::NoteBound(
    _In_ UINT Slot,
    _In_opt_ HANDLE Resource)
{
    Bound[Slot] = Resource;
    Reference(Resource);
}

VOID
D9Batch::Run(
    _Inout_ D9BatchBuffer* Buffer)
{
    UINT Offset = 0;

    while (Offset < Buffer->Used)
    {
        D9BatchRecord* Header = reinterpret_cast<D9BatchRecord*>(Buffer->Data + Offset);
        BYTE* Args = reinterpret_cast<BYTE*>(Header + 1);

        switch (Header->Call)
        {
        case D9BatchCall::RenderState:
            Funcs->pfnSetRenderState(Driver, reinterpret_cast<D3DDDIARG_RENDERSTATE*>(Args));
            break;
        case D9BatchCall::TextureStageState:
            Funcs->pfnSetTextureStageState(Driver, reinterpret_cast<D3DDDIARG_TEXTURESTAGESTATE*>(Args));
            break;
        case D9BatchCall::PixelShaderConst:
        {
            D3DDDIARG_SETPIXELSHADERCONST* Const = reinterpret_cast<D3DDDIARG_SETPIXELSHADERCONST*>(Args);

            Funcs->pfnSetPixelShaderConst(Driver, Const, reinterpret_cast<FLOAT*>(Const + 1));
            break;
        }
        case D9BatchCall::Viewport:
            Funcs->pfnSetViewport(Driver, reinterpret_cast<D3DDDIARG_VIEWPORTINFO*>(Args));
            break;
        case D9BatchCall::ZRange:
            Funcs->pfnSetZRange(Driver, reinterpret_cast<D3DDDIARG_ZRANGE*>(Args));
            break;
        case D9BatchCall::ScissorRect:
            Funcs->pfnSetScissorRect(Driver, reinterpret_cast<RECT*>(Args));
            break;
        case D9BatchCall::ClipPlane:
            Funcs->pfnSetClipPlane(Driver, reinterpret_cast<D3DDDIARG_SETCLIPPLANE*>(Args));
            break;
        case D9BatchCall::WInfo:
            Funcs->pfnUpdateWInfo(Driver, reinterpret_cast<D3DDDIARG_WINFO*>(Args));
            break;
        case D9BatchCall::StreamSourceFreq:
            Funcs->pfnSetStreamSourceFreq(Driver, reinterpret_cast<D3DDDIARG_SETSTREAMSOURCEFREQ*>(Args));
            break;
        case D9BatchCall::RenderTarget:
            Funcs->pfnSetRenderTarget(Driver, reinterpret_cast<D3DDDIARG_SETRENDERTARGET*>(Args));
            break;
        case D9BatchCall::DepthStencil:
            Funcs->pfnSetDepthStencil(Driver, reinterpret_cast<D3DDDIARG_SETDEPTHSTENCIL*>(Args));
            break;
        case D9BatchCall::Clear:
        {
            D3DDDIARG_CLEAR* Clear = reinterpret_cast<D3DDDIARG_CLEAR*>(Args);
            UINT* Count = reinterpret_cast<UINT*>(Clear + 1);

            Funcs->pfnClear(Driver, Clear, *Count, reinterpret_cast<RECT*>(Count + 2));
            break;
        }
        case D9BatchCall::Present1:
        {
            /* The driver is flushed before a recorded present reaches it */
            D3DDDIARG_PRESENT1* Present = reinterpret_cast<D3DDDIARG_PRESENT1*>(Args);

            Present->phSrcResources = reinterpret_cast<D3DDDIARG_PRESENTSURFACE*>(Present + 1);
            Funcs->pfnFlush1(Driver, 0);
            Funcs->pfnPresent1(Driver, Present);
            break;
        }
        case D9BatchCall::Rename:
            Funcs->pfnRename(Driver, reinterpret_cast<D3DDDIARG_RENAME*>(Args));
            break;
        case D9BatchCall::Blt:
            Funcs->pfnBlt(Driver, reinterpret_cast<D3DDDIARG_BLT*>(Args));
            break;
        case D9BatchCall::ColorFill:
            Funcs->pfnColorFill(Driver, reinterpret_cast<D3DDDIARG_COLORFILL*>(Args));
            break;
        case D9BatchCall::VertexShaderFunc:
        case D9BatchCall::VertexShaderConst:
        case D9BatchCall::VertexShaderConstI:
        case D9BatchCall::VertexShaderConstB:
        case D9BatchCall::PixelShaderConstI:
        case D9BatchCall::PixelShaderConstB:
            D9RunShaderConstants(Funcs, Driver, Header->Call, Args);
            break;
        case D9BatchCall::Texture:
        {
            D9TextureRecord* Record = reinterpret_cast<D9TextureRecord*>(Args);

            Funcs->pfnSetTexture(Driver, Record->Stage, Record->Resource);
            break;
        }
        case D9BatchCall::StreamSource:
            Funcs->pfnSetStreamSource(Driver, reinterpret_cast<D3DDDIARG_SETSTREAMSOURCE*>(Args));
            break;
        case D9BatchCall::Indices:
            Funcs->pfnSetIndices(Driver, reinterpret_cast<D3DDDIARG_SETINDICES*>(Args));
            break;
        case D9BatchCall::VertexShaderDecl:
            Funcs->pfnSetVertexShaderDecl(Driver, *reinterpret_cast<HANDLE*>(Args));
            break;
        case D9BatchCall::PixelShader:
            Funcs->pfnSetPixelShader(Driver, *reinterpret_cast<HANDLE*>(Args));
            break;
        case D9BatchCall::DrawPrimitive:
            Funcs->pfnDrawPrimitive(Driver, reinterpret_cast<D3DDDIARG_DRAWPRIMITIVE*>(Args), nullptr);
            break;
        case D9BatchCall::DrawIndexedPrimitive:
            Funcs->pfnDrawIndexedPrimitive(Driver, reinterpret_cast<D3DDDIARG_DRAWINDEXEDPRIMITIVE*>(Args));
            break;
        case D9BatchCall::StreamSourceUm:
        {
            D9MemoryRecord* Record = reinterpret_cast<D9MemoryRecord*>(Args);
            D3DDDIARG_SETSTREAMSOURCEUM Stream = {};

            Stream.Stride = Record->Stride;
            Funcs->pfnSetStreamSourceUm(Driver, &Stream, Record->Data);
            break;
        }
        case D9BatchCall::IndicesUm:
        {
            D9MemoryRecord* Record = reinterpret_cast<D9MemoryRecord*>(Args);

            Funcs->pfnSetIndicesUm(Driver, Record->Stride, Record->Data);
            break;
        }
        case D9BatchCall::BufBlt:
            D9RunBufBlt(Funcs, Driver, reinterpret_cast<D3DDDIARG_BUFFERBLT1*>(Args));
            break;
        case D9BatchCall::TexBlt:
            D9RunTexBlt(Funcs, Driver, reinterpret_cast<D3DDDIARG_TEXBLT1*>(Args));
            break;
        case D9BatchCall::VolBlt:
            D9RunVolBlt(Funcs, Driver, reinterpret_cast<D3DDDIARG_VOLUMEBLT1*>(Args));
            break;
        case D9BatchCall::GenerateMips:
            Funcs->pfnGenerateMipSubLevels(Driver, reinterpret_cast<D3DDDIARG_GENERATEMIPSUBLEVELS*>(Args));
            break;
        case D9BatchCall::DrawFill:
        {
            D9FillDrawRecord* Record = reinterpret_cast<D9FillDrawRecord*>(Args);

            if (SUCCEEDED(Funcs->pfnSetStreamSource(Driver, &Record->Stream)))
                Funcs->pfnDrawPrimitive2(Driver, &Record->Draw);
            break;
        }
        case D9BatchCall::DrawIndexedFill:
        {
            D9FillIndexedRecord* Record = reinterpret_cast<D9FillIndexedRecord*>(Args);

            if (SUCCEEDED(Funcs->pfnSetStreamSource(Driver, &Record->Stream)) &&
                SUCCEEDED(Funcs->pfnSetIndices(Driver, &Record->Indices)))
            {
                Funcs->pfnDrawIndexedPrimitive(Driver, &Record->Draw);
            }
            break;
        }
        }
        Offset += Header->Size;
    }
    Buffer->Used = 0;
    Buffer->Calls = 0;
}
