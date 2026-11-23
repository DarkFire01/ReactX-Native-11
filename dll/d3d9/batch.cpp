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
    D9Resource* Targets[3] = { &Buffer->Vertices, &Buffer->Indices16, &Buffer->Indices32 };
    HRESULT hr;

    for (UINT Index = 0; Index < _countof(Fills); Index++)
    {
        D3DDDI_POOL Placement = Device->BufferPlacement(Index == 0, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                                        D3DPOOL_DEFAULT);

        if (Placement != D3DDDIPOOL_SYSTEMMEM)
        {
            hr = Device->CreateBufferResource(static_cast<D3DFORMAT>(Fills[Index].Format), Fills[Index].Size,
                                              Fills[Index].Flags, 0, Placement, nullptr, Targets[Index]);
            if (FAILED(hr))
                return hr;
            continue;
        }

        Buffer->Memory[Index] = VirtualAlloc(nullptr, Fills[Index].Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (Buffer->Memory[Index] == nullptr)
            return E_OUTOFMEMORY;

        hr = Device->CreateSystemBuffer(Fills[Index].Format, Fills[Index].Size, Fills[Index].Flags,
                                        Buffer->Memory[Index], Targets[Index]);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* The fills stay locked while the main thread records into their buffer; system memory ones only tell the driver */
VOID
D9Batch::LockFills(
    _Inout_ D9BatchBuffer* Buffer)
{
    D9Resource* Targets[3] = { &Buffer->Vertices, &Buffer->Indices16, &Buffer->Indices32 };

    for (D9Resource* Fill : Targets)
    {
        D3DDDIARG_LOCK Lock = {};

        Lock.hResource = Fill->DriverResource;
        Lock.Flags.Discard = TRUE;
        Lock.Flags.NotifyOnly = Fill->Pool == D3DDDIPOOL_SYSTEMMEM;
        if (FAILED(Funcs->pfnLock(Driver, &Lock)))
            D9_TRACE_ONCE(L"the driver would not lock a batch fill");
    }
}

VOID
D9Batch::DestroyFills(
    _Inout_ D9BatchBuffer* Buffer)
{
    D9Resource* Targets[3] = { &Buffer->Vertices, &Buffer->Indices16, &Buffer->Indices32 };

    /* Index fills in video memory are unlocked before they go; the vertex fill goes still locked */
    for (UINT Index = 0; Index < _countof(Targets); Index++)
    {
        if (Index != 0 && Targets[Index]->DriverResource != NULL && Targets[Index]->Pool != D3DDDIPOOL_SYSTEMMEM)
        {
            D3DDDIARG_UNLOCK Unlock = {};

            Unlock.hResource = Targets[Index]->DriverResource;
            Funcs->pfnUnlock(Driver, &Unlock);
        }
        if (Targets[Index]->DriverResource != NULL)
            Funcs->pfnDestroyResource(Driver, Targets[Index]->DriverResource);
        Targets[Index]->DriverResource = NULL;
        if (Buffer->Memory[Index] != nullptr)
            VirtualFree(Buffer->Memory[Index], 0, MEM_RELEASE);
        Buffer->Memory[Index] = nullptr;
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

    /* The idle buffer is locked first, then the one the main thread records into */
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

/* Hands the recording buffer to the worker and records into the other one */
VOID
D9Batch::Submit(VOID)
{
    WaitForWorker();
    Current ^= 1;
    Buffers[Current].Used = 0;
    Buffers[Current].Calls = 0;
    InterlockedExchange(&Busy, 1);
    Pending = TRUE;
    SetEvent(Wake);
    Advance();
}

/* Waits for the worker, then runs what is recorded on the calling thread */
VOID
D9Batch::Drain(VOID)
{
    WaitForWorker();
    Run(&Buffers[Current]);
    Advance();
}

/* A new generation starts with the bound targets already in use */
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
        }
        Offset += Header->Size;
    }
    Buffer->Used = 0;
    Buffer->Calls = 0;
}
