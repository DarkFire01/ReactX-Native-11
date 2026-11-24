/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Draws through the system d3d9.dll, read back pixel by pixel
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <d3d9.h>
#include <d3dumddi.h>

#include <stdio.h>

#include "checks.hpp"

/* A pretransformed vertex with a color */
struct CkTlVertex
{
    FLOAT X;
    FLOAT Y;
    FLOAT Z;
    FLOAT Rhw;
    DWORD Color;
};

static constexpr DWORD CkTlFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

/* Filled and locked before each readback so the runtime batch drains at a fixed point */
static IDirect3DSurface9* CkDrawFlush;

/* Set while the runtime drains on its own thread, where a print can land among the drained calls */
static BOOLEAN CkDrawQuiet;

/* Empties the runtime batch without printing */
static
VOID
CkDrainQuietly(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Flush)
{
    D3DLOCKED_RECT Locked;

    Device->ColorFill(Flush, nullptr, 0);
    if (SUCCEEDED(Flush->LockRect(&Locked, nullptr, D3DLOCK_READONLY)))
        Flush->UnlockRect();
}

/* Prints every row of the target as a fingerprint so a difference shows where it is */
static
VOID
CkReadRows(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Target,
    _In_z_ PCSTR Label)
{
    IDirect3DSurface9* Copy = nullptr;
    D3DLOCKED_RECT Locked = {};
    D3DSURFACE_DESC Desc = {};
    HRESULT hr;

    if (CkDrawFlush != nullptr && CkDrawQuiet)
        CkDrainQuietly(Device, CkDrawFlush);
    else if (CkDrawFlush != nullptr)
        CkFlushBatch(Device, CkDrawFlush, Label);

    Target->GetDesc(&Desc);
    hr = Device->CreateOffscreenPlainSurface(Desc.Width, Desc.Height, Desc.Format, D3DPOOL_SYSTEMMEM, &Copy, nullptr);
    if (FAILED(hr))
    {
        printf("%s CreateOffscreenPlainSurface hr %08lX\n", Label, hr);
        return;
    }

    hr = Device->GetRenderTargetData(Target, Copy);
    if (SUCCEEDED(hr))
        hr = Copy->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        printf("%s readback hr %08lX\n", Label, hr);
        Copy->Release();
        return;
    }

    for (UINT Row = 0; Row < Desc.Height; Row += 4)
    {
        const DWORD* Line = reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(Locked.pBits) + Row * Locked.Pitch);

        printf("%s row %2u:", Label, Row);
        for (UINT Column = 0; Column < Desc.Width; Column += 4)
            printf(" %08lX", Line[Column]);
        printf("\n");
    }
    Copy->UnlockRect();
    Copy->Release();
}

/* Triangles in screen space: one from user memory, a strip from a vertex buffer */
static
VOID
CkPretransformed(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    static const CkTlVertex Triangle[] =
    {
        { 4.0f, 4.0f, 0.5f, 1.0f, 0xFFFF0000 },
        { 60.0f, 8.0f, 0.5f, 1.0f, 0xFF00FF00 },
        { 12.0f, 44.0f, 0.5f, 1.0f, 0xFF0000FF },
    };
    static const CkTlVertex Strip[] =
    {
        { 32.0f, 24.0f, 0.25f, 1.0f, 0x80FFFFFF },
        { 64.0f, 24.0f, 0.25f, 1.0f, 0x80FF00FF },
        { 32.0f, 48.0f, 0.25f, 1.0f, 0x8000FFFF },
        { 64.0f, 48.0f, 0.25f, 1.0f, 0x80FFFF00 },
    };
    IDirect3DVertexBuffer9* Buffer = nullptr;
    VOID* Data = nullptr;
    HRESULT hr;

    hr = Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF202020, 1.0f, 0);
    printf("pretransformed Clear hr %08lX\n", hr);
    hr = Device->SetFVF(CkTlFvf);
    printf("pretransformed SetFVF hr %08lX\n", hr);
    hr = Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    printf("pretransformed SetRenderState hr %08lX\n", hr);
    hr = Device->BeginScene();
    printf("pretransformed BeginScene hr %08lX\n", hr);
    hr = Device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, Triangle, sizeof(Triangle[0]));
    printf("pretransformed DrawPrimitiveUP hr %08lX\n", hr);

    hr = Device->CreateVertexBuffer(sizeof(Strip), D3DUSAGE_WRITEONLY, CkTlFvf, D3DPOOL_DEFAULT, &Buffer, nullptr);
    printf("pretransformed CreateVertexBuffer hr %08lX\n", hr);
    if (Buffer != nullptr)
    {
        if (CkDrawFlush != nullptr)
            CkFlushBatch(Device, CkDrawFlush, "pretransformed");
        hr = Buffer->Lock(0, 0, &Data, 0);
        if (SUCCEEDED(hr))
        {
            CopyMemory(Data, Strip, sizeof(Strip));
            Buffer->Unlock();
        }
        hr = Device->SetStreamSource(0, Buffer, 0, sizeof(Strip[0]));
        printf("pretransformed SetStreamSource hr %08lX\n", hr);
        hr = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
        printf("pretransformed DrawPrimitive hr %08lX\n", hr);
    }
    hr = Device->EndScene();
    printf("pretransformed EndScene hr %08lX\n", hr);
    CkReadRows(Device, Back, "pretransformed");

    Device->SetStreamSource(0, nullptr, 0, 0);
    if (Buffer != nullptr)
        Buffer->Release();
}

/* Screen space vertices with one texture coordinate */
struct CkTexVertex
{
    FLOAT X;
    FLOAT Y;
    FLOAT Z;
    FLOAT Rhw;
    DWORD Color;
    FLOAT U;
    FLOAT V;
};

static constexpr DWORD CkTexFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

/* A vertex the fixed function pipeline transforms */
struct CkPlainVertex
{
    FLOAT X;
    FLOAT Y;
    FLOAT Z;
    DWORD Color;
};

static constexpr DWORD CkPlainFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

/* vs_2_0: position through, color scaled by c1 */
static const DWORD CkScaleVertexCode[] =
{
    0xFFFE0200,
    0x0200001F, 0x80000000, 0x900F0000,
    0x0200001F, 0x8000000A, 0x900F0001,
    0x02000001, 0xC00F0000, 0x90E40000,
    0x03000005, 0xD00F0000, 0x90E40001, 0xA0E40001,
    0x0000FFFF,
};

/* ps_2_0: the interpolated color times c0 */
static const DWORD CkScalePixelCode[] =
{
    0xFFFF0200,
    0x0200001F, 0x80000000, 0x900F0000,
    0x03000005, 0x800F0000, 0x90E40000, 0xA0E40000,
    0x02000001, 0x800F0800, 0x80E40000,
    0x0000FFFF,
};

/* ps_2_0: c1 as the shader defines it */
static const DWORD CkDefinedPixelCode[] =
{
    0xFFFF0200,
    0x05000051, 0xA00F0001, 0x3E800000, 0x3F000000, 0x3F400000, 0x3F800000,
    0x02000001, 0x800F0800, 0xA0E40001,
    0x0000FFFF,
};

/* ps_2_0: c1 as the constants hold it */
static const DWORD CkReaderPixelCode[] =
{
    0xFFFF0200,
    0x02000001, 0x800F0800, 0xA0E40001,
    0x0000FFFF,
};

/* vs_2_0: position moved by the instance offset in v2, color through */
static const DWORD CkInstanceVertexCode[] =
{
    0xFFFE0200,
    0x0200001F, 0x80000000, 0x900F0000,
    0x0200001F, 0x8000000A, 0x900F0001,
    0x0200001F, 0x80010005, 0x900F0002,
    0x02000001, 0x800F0000, 0x90E40002,
    0x03000002, 0xC00F0000, 0x90E40000, 0x80E40000,
    0x02000001, 0xD00F0000, 0x90E40001,
    0x0000FFFF,
};

static
VOID
CkRun(
    _In_ HRESULT hr,
    _In_z_ PCSTR Label,
    _In_z_ PCSTR Step)
{
    printf("%s %s hr %08lX\n", Label, Step, hr);
}

static
VOID
CkQuad(
    _Out_writes_(4) CkTexVertex* Quad,
    _In_ FLOAT Left,
    _In_ FLOAT Top,
    _In_ FLOAT Right,
    _In_ FLOAT Bottom,
    _In_ FLOAT Z,
    _In_ DWORD Color,
    _In_ FLOAT Repeat)
{
    const CkTexVertex Corners[4] =
    {
        { Left, Top, Z, 1.0f, Color, 0.0f, 0.0f },
        { Right, Top, Z, 1.0f, Color, Repeat, 0.0f },
        { Left, Bottom, Z, 1.0f, Color, 0.0f, Repeat },
        { Right, Bottom, Z, 1.0f, Color, Repeat, Repeat },
    };

    memcpy(Quad, Corners, sizeof(Corners));
}

static
DWORD
CkFloatBits(
    _In_ FLOAT Value)
{
    DWORD Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

/* Application shaders with constants, an index buffer and instancing */
static
VOID
CkShaderDraws(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    static const D3DVERTEXELEMENT9 Plain[] =
    {
        { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        D3DDECL_END(),
    };
    static const D3DVERTEXELEMENT9 Instanced[] =
    {
        { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        { 1, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
        D3DDECL_END(),
    };
    static const CkPlainVertex Quad[] =
    {
        { -0.9f, 0.9f, 0.5f, 0xFFFF8040 },
        { 0.1f, 0.9f, 0.5f, 0xFF40FF80 },
        { -0.9f, -0.1f, 0.5f, 0xFF8040FF },
        { 0.1f, -0.1f, 0.5f, 0xFFFFFFFF },
    };
    static const WORD Indices[] = { 0, 1, 2, 2, 1, 3 };
    static const FLOAT Offsets[][4] = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.8f, -0.8f, 0.0f, 0.0f } };
    static const FLOAT PixelScale[4] = { 0.5f, 1.0f, 0.75f, 1.0f };
    static const FLOAT VertexScale[4] = { 1.0f, 0.5f, 1.0f, 1.0f };
    IDirect3DVertexShader9* Vertex = nullptr;
    IDirect3DVertexShader9* Instance = nullptr;
    IDirect3DPixelShader9* Pixel = nullptr;
    IDirect3DVertexDeclaration9* Decls[2] = {};
    IDirect3DVertexBuffer9* Buffers[2] = {};
    IDirect3DIndexBuffer9* IndexBuffer = nullptr;
    VOID* Data;

    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000040, 1.0f, 0), "shader", "Clear");
    CkRun(Device->CreateVertexShader(CkScaleVertexCode, &Vertex), "shader", "CreateVertexShader");
    CkRun(Device->CreateVertexShader(CkInstanceVertexCode, &Instance), "shader", "CreateVertexShader instanced");
    CkRun(Device->CreatePixelShader(CkScalePixelCode, &Pixel), "shader", "CreatePixelShader");
    CkRun(Device->CreateVertexDeclaration(Plain, &Decls[0]), "shader", "CreateVertexDeclaration");
    CkRun(Device->CreateVertexDeclaration(Instanced, &Decls[1]), "shader", "CreateVertexDeclaration instanced");
    CkRun(Device->CreateVertexBuffer(sizeof(Quad), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &Buffers[0], nullptr), "shader",
          "CreateVertexBuffer");
    CkRun(Device->CreateVertexBuffer(sizeof(Offsets), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &Buffers[1], nullptr),
          "shader", "CreateVertexBuffer instances");
    CkRun(Device->CreateIndexBuffer(sizeof(Indices), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &IndexBuffer,
                                    nullptr),
          "shader", "CreateIndexBuffer");
    if (Vertex == nullptr || Instance == nullptr || Pixel == nullptr || Decls[0] == nullptr || Decls[1] == nullptr ||
        Buffers[0] == nullptr || Buffers[1] == nullptr || IndexBuffer == nullptr)
    {
        goto Done;
    }

    if (CkDrawFlush != nullptr)
        CkFlushBatch(Device, CkDrawFlush, "shader");
    if (SUCCEEDED(Buffers[0]->Lock(0, 0, &Data, 0)))
    {
        memcpy(Data, Quad, sizeof(Quad));
        Buffers[0]->Unlock();
    }
    if (SUCCEEDED(Buffers[1]->Lock(0, 0, &Data, 0)))
    {
        memcpy(Data, Offsets, sizeof(Offsets));
        Buffers[1]->Unlock();
    }
    if (SUCCEEDED(IndexBuffer->Lock(0, 0, &Data, 0)))
    {
        memcpy(Data, Indices, sizeof(Indices));
        IndexBuffer->Unlock();
    }

    Device->BeginScene();
    Device->SetVertexDeclaration(Decls[0]);
    Device->SetVertexShader(Vertex);
    Device->SetPixelShader(Pixel);
    Device->SetVertexShaderConstantF(1, VertexScale, 1);
    Device->SetPixelShaderConstantF(0, PixelScale, 1);
    Device->SetStreamSource(0, Buffers[0], 0, sizeof(Quad[0]));
    Device->SetIndices(IndexBuffer);
    CkRun(Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2), "shader", "DrawIndexedPrimitive");

    Device->SetVertexDeclaration(Decls[1]);
    Device->SetVertexShader(Instance);
    Device->SetStreamSource(1, Buffers[1], 0, sizeof(Offsets[0]));
    Device->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 2);
    Device->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1);
    CkRun(Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2), "shader", "DrawIndexedPrimitive instanced");
    Device->SetStreamSourceFreq(0, 1);
    Device->SetStreamSourceFreq(1, 1);
    Device->EndScene();
    CkReadRows(Device, Back, "shader");

Done:
    Device->SetStreamSource(0, nullptr, 0, 0);
    Device->SetStreamSource(1, nullptr, 0, 0);
    Device->SetIndices(nullptr);
    Device->SetVertexShader(nullptr);
    Device->SetPixelShader(nullptr);
    Device->SetVertexDeclaration(nullptr);
    if (IndexBuffer != nullptr)
        IndexBuffer->Release();
    for (IDirect3DVertexBuffer9* Buffer : Buffers)
    {
        if (Buffer != nullptr)
            Buffer->Release();
    }
    for (IDirect3DVertexDeclaration9* Decl : Decls)
    {
        if (Decl != nullptr)
            Decl->Release();
    }
    if (Pixel != nullptr)
        Pixel->Release();
    if (Instance != nullptr)
        Instance->Release();
    if (Vertex != nullptr)
        Vertex->Release();
}

/* The fixed function pipeline as the runtime turns it into shaders: fans, user memory indices, wireframe, lines */
static
VOID
CkFixedFunction(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    static const CkPlainVertex Fan[] =
    {
        { -0.5f, 0.0f, 0.5f, 0xFFFFFFFF },
        { -1.0f, 0.8f, 0.5f, 0xFFFF0000 },
        { 0.0f, 0.8f, 0.5f, 0xFF00FF00 },
        { 0.2f, 0.0f, 0.5f, 0xFF0000FF },
        { 0.0f, -0.8f, 0.5f, 0xFFFFFF00 },
    };
    static const CkPlainVertex Corner[] =
    {
        { 0.3f, -0.2f, 0.5f, 0xFF00FFFF },
        { 1.0f, -0.2f, 0.5f, 0xFFFF00FF },
        { 0.3f, -1.0f, 0.5f, 0xFF808080 },
        { 1.0f, -1.0f, 0.5f, 0xFF000000 },
    };
    static const WORD Indices[] = { 0, 1, 2, 2, 1, 3 };
    static const WORD FanIndices[] = { 0, 1, 2, 3, 4 };

    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1.0f, 0), "fixed", "Clear");
    Device->SetFVF(CkPlainFvf);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Device->BeginScene();
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 3, Fan, sizeof(Fan[0])), "fixed", "DrawPrimitiveUP fan");
    CkRun(Device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, Indices, D3DFMT_INDEX16, Corner, sizeof(Corner[0])),
          "fixed", "DrawIndexedPrimitiveUP");
    CkRun(Device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLEFAN, 0, 5, 3, FanIndices, D3DFMT_INDEX16, Fan, sizeof(Fan[0])),
          "fixed", "DrawIndexedPrimitiveUP fan");
    Device->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Corner, sizeof(Corner[0])), "fixed", "DrawPrimitiveUP wireframe");
    Device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    CkRun(Device->DrawPrimitiveUP(D3DPT_LINESTRIP, 3, Corner, sizeof(Corner[0])), "fixed", "DrawPrimitiveUP lines");
    Device->EndScene();
    CkReadRows(Device, Back, "fixed");
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
}

/*
 * Fans from an index buffer. The driver keeps the list it made with the buffer and only checks
 * the start, so a longer fan from the same start reuses the shorter list until the buffer is
 * written or the start moves.
 */
static
VOID
CkIndexedFans(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    static const CkPlainVertex Fan[] =
    {
        { 0.0f, 0.0f, 0.5f, 0xFFFFFFFF },
        { -0.9f, 0.9f, 0.5f, 0xFFFF0000 },
        { 0.9f, 0.9f, 0.5f, 0xFF00FF00 },
        { 0.9f, -0.9f, 0.5f, 0xFF0000FF },
        { -0.9f, -0.9f, 0.5f, 0xFFFFFF00 },
        { -0.9f, 0.9f, 0.5f, 0xFFFF00FF },
    };
    static const WORD Indices[] = { 0, 1, 2, 3, 4, 5 };
    IDirect3DVertexBuffer9* Vertices = nullptr;
    IDirect3DIndexBuffer9* IndexBuffer = nullptr;
    VOID* Data;

    CkRun(Device->CreateVertexBuffer(sizeof(Fan), D3DUSAGE_WRITEONLY, CkPlainFvf, D3DPOOL_DEFAULT, &Vertices, nullptr),
          "fans", "CreateVertexBuffer");
    CkRun(Device->CreateIndexBuffer(sizeof(Indices), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &IndexBuffer,
                                    nullptr),
          "fans", "CreateIndexBuffer");
    if (Vertices == nullptr || IndexBuffer == nullptr)
        goto Done;

    if (CkDrawFlush != nullptr)
        CkFlushBatch(Device, CkDrawFlush, "fans");
    if (SUCCEEDED(Vertices->Lock(0, 0, &Data, 0)))
    {
        memcpy(Data, Fan, sizeof(Fan));
        Vertices->Unlock();
    }
    if (SUCCEEDED(IndexBuffer->Lock(0, 0, &Data, 0)))
    {
        memcpy(Data, Indices, sizeof(Indices));
        IndexBuffer->Unlock();
    }

    Device->SetFVF(CkPlainFvf);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Device->SetStreamSource(0, Vertices, 0, sizeof(Fan[0]));
    Device->SetIndices(IndexBuffer);

    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "fans", "Clear");
    Device->BeginScene();
    CkRun(Device->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 0, 6, 0, 2), "fans", "two");
    CkRun(Device->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 0, 6, 0, 4), "fans", "four from the same start");
    Device->EndScene();
    CkReadRows(Device, Back, "fans reused");

    if (SUCCEEDED(IndexBuffer->Lock(0, 0, &Data, D3DLOCK_READONLY)))
        IndexBuffer->Unlock();
    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "fans", "Clear");
    Device->BeginScene();
    CkRun(Device->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 0, 6, 0, 4), "fans", "four after a read lock");
    Device->EndScene();
    CkReadRows(Device, Back, "fans read");

    if (CkDrawFlush != nullptr)
        CkFlushBatch(Device, CkDrawFlush, "fans");
    if (SUCCEEDED(IndexBuffer->Lock(0, 0, &Data, 0)))
    {
        memcpy(Data, Indices, sizeof(Indices));
        IndexBuffer->Unlock();
    }
    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "fans", "Clear");
    Device->BeginScene();
    CkRun(Device->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 0, 6, 0, 4), "fans", "four after a write");
    Device->EndScene();
    CkReadRows(Device, Back, "fans written");

    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "fans", "Clear");
    Device->BeginScene();
    CkRun(Device->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 0, 6, 1, 2), "fans", "two from the next start");
    CkRun(Device->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 0, 6, 0, 3), "fans", "three from the first start");
    Device->EndScene();
    CkReadRows(Device, Back, "fans moved");

Done:
    Device->SetStreamSource(0, nullptr, 0, 0);
    Device->SetIndices(nullptr);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    if (IndexBuffer != nullptr)
        IndexBuffer->Release();
    if (Vertices != nullptr)
        Vertices->Release();
}

/* A checkered texture sampled with point and linear filters, wrapped and clamped, then blended */
static
VOID
CkTextured(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    IDirect3DTexture9* Texture = nullptr;
    D3DLOCKED_RECT Locked;
    CkTexVertex Quad[4];

    CkRun(Device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &Texture, nullptr), "texture",
          "CreateTexture");
    if (Texture == nullptr)
        return;
    if (SUCCEEDED(Texture->LockRect(0, &Locked, nullptr, 0)))
    {
        for (UINT Row = 0; Row < 4; Row += 1)
        {
            DWORD* Line = reinterpret_cast<DWORD*>(static_cast<BYTE*>(Locked.pBits) + Row * Locked.Pitch);

            for (UINT Column = 0; Column < 4; Column += 1)
                Line[Column] = ((Row ^ Column) & 1) ? 0x80FF4000 : 0xFF00C0FF;
        }
        Texture->UnlockRect(0);
    }

    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "texture", "Clear");
    Device->SetFVF(CkTexFvf);
    Device->SetTexture(0, Texture);
    Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device->BeginScene();

    Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    CkQuad(Quad, 0.0f, 0.0f, 32.0f, 24.0f, 0.5f, 0xFFFFFFFF, 2.0f);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "texture", "point wrap");

    Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    CkQuad(Quad, 32.0f, 0.0f, 64.0f, 24.0f, 0.5f, 0xFFFFFFFF, 1.0f);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "texture", "linear");

    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR);
    CkQuad(Quad, 0.0f, 24.0f, 32.0f, 48.0f, 0.5f, 0xFFFF8080, 2.0f);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "texture", "clamp mirror");

    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    CkQuad(Quad, 24.0f, 16.0f, 64.0f, 48.0f, 0.5f, 0xC0FFFFFF, 1.0f);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "texture", "blended");
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    Device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT);
    CkQuad(Quad, 40.0f, 0.0f, 56.0f, 40.0f, 0.5f, 0xFF404040, 1.0f);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "texture", "subtracted");
    Device->EndScene();
    CkReadRows(Device, Back, "texture");

    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    Device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    Device->SetTexture(0, nullptr);
    Texture->Release();
}

/* Depth testing, stencil writes and a stencil masked fill, then table fog and alpha testing */
static
VOID
CkDepthStencilFog(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    IDirect3DSurface9* Depth = nullptr;
    CkTexVertex Quad[4];

    CkRun(Device->CreateDepthStencilSurface(64, 48, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Depth, nullptr),
          "depth", "CreateDepthStencilSurface");
    if (Depth == nullptr)
        return;
    Device->SetDepthStencilSurface(Depth);
    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFF000000, 1.0f, 0), "depth",
          "Clear");
    Device->SetFVF(CkTexFvf);
    Device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    Device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
    Device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    Device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    Device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
    Device->SetRenderState(D3DRS_STENCILREF, 1);
    Device->BeginScene();

    CkQuad(Quad, 8.0f, 8.0f, 40.0f, 40.0f, 0.5f, 0xFFFF0000, 1.0f);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "depth", "near");
    Device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    CkQuad(Quad, 24.0f, 16.0f, 56.0f, 44.0f, 0.75f, 0xFF00FF00, 1.0f);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "depth", "far");

    Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    Device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    Device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    Device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    CkQuad(Quad, 0.0f, 0.0f, 32.0f, 48.0f, 0.5f, 0xFF0000FF, 1.0f);
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "depth", "stencil masked");
    Device->SetRenderState(D3DRS_STENCILENABLE, FALSE);

    Device->SetRenderState(D3DRS_FOGENABLE, TRUE);
    Device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
    Device->SetRenderState(D3DRS_FOGCOLOR, 0xFF808000);
    Device->SetRenderState(D3DRS_FOGSTART, CkFloatBits(0.25f));
    Device->SetRenderState(D3DRS_FOGEND, CkFloatBits(0.75f));
    CkQuad(Quad, 40.0f, 0.0f, 64.0f, 24.0f, 0.5f, 0xFFFFFFFF, 1.0f);
    Quad[1].Z = 0.1f;
    Quad[3].Z = 0.9f;
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "depth", "fog");
    Device->SetRenderState(D3DRS_FOGENABLE, FALSE);

    Device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    Device->SetRenderState(D3DRS_ALPHAREF, 0x80);
    Device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    CkQuad(Quad, 40.0f, 24.0f, 64.0f, 48.0f, 0.5f, 0xFFFFFFFF, 1.0f);
    Quad[0].Color = 0x00FF00FF;
    Quad[2].Color = 0x00FF00FF;
    CkRun(Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0])), "depth", "alpha test");
    Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    Device->EndScene();
    CkReadRows(Device, Back, "depth");

    Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    Device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    Device->SetDepthStencilSurface(nullptr);
    Depth->Release();
}

/* Large points as sprites, which take the geometry stage */
static
VOID
CkPointSprites(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    static const CkTexVertex Points[] =
    {
        { 8.0f, 8.0f, 0.5f, 1.0f, 0xFFFF0000, 0.0f, 0.0f },
        { 32.0f, 24.0f, 0.5f, 1.0f, 0xFF00FF00, 0.0f, 0.0f },
        { 56.0f, 40.0f, 0.5f, 1.0f, 0xFF0000FF, 0.0f, 0.0f },
    };

    CkRun(Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0), "points", "Clear");
    Device->SetFVF(CkTexFvf);
    Device->BeginScene();
    CkRun(Device->DrawPrimitiveUP(D3DPT_POINTLIST, 3, Points, sizeof(Points[0])), "points", "small");
    Device->SetRenderState(D3DRS_POINTSIZE, CkFloatBits(8.0f));
    Device->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
    CkRun(Device->DrawPrimitiveUP(D3DPT_POINTLIST, 3, Points, sizeof(Points[0])), "points", "sprites");
    Device->EndScene();
    CkReadRows(Device, Back, "points");
    Device->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
    Device->SetRenderState(D3DRS_POINTSIZE, CkFloatBits(1.0f));
}

/*
 * The defined value of a register is copied into the constants only when something the shader is
 * keyed on changed. The shader reads it as a literal, so a second shader that reads the register
 * shows which value the constants hold: each column draws the defining shader, changes one thing,
 * sets the register, draws it again and then draws the reader over the column.
 */
static
VOID
CkPixelDefines(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    IDirect3DPixelShader9* Defined = nullptr;
    IDirect3DPixelShader9* Reader = nullptr;
    CkTexVertex Quad[4];
    PCSTR Steps[16] = {};
    HRESULT Results[16] = {};
    HRESULT Cleared;
    UINT Column = 0;

    CkRun(Device->CreatePixelShader(CkDefinedPixelCode, &Defined), "defines", "CreatePixelShader");
    CkRun(Device->CreatePixelShader(CkReaderPixelCode, &Reader), "defines", "CreatePixelShader reader");
    if (Defined == nullptr || Reader == nullptr)
        goto Done;
    /* A pure device drains its batch on another thread, so nothing prints until it is drained */
    Cleared = Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetFVF(CkTexFvf);
    if (CkDrawFlush != nullptr)
        CkDrainQuietly(Device, CkDrawFlush);
    Device->BeginScene();

    {
        auto Begin = [&]()
        {
            FLOAT Left = static_cast<FLOAT>((Column % 8) * 8);
            FLOAT Top = static_cast<FLOAT>((Column / 8) * 24);

            CkQuad(Quad, Left, Top, Left + 8.0f, Top + 24.0f, 0.5f, 0xFFFFFFFF, 1.0f);
            Device->SetPixelShader(Defined);
            Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0]));
        };
        auto Finish = [&](PCSTR Step)
        {
            FLOAT Own[4] = { static_cast<FLOAT>(Column + 1) / 16.0f, 0.0f, 1.0f, 1.0f };

            /* A new value each time, since the runtime drops a set that repeats the last one */
            Device->SetPixelShaderConstantF(1, Own, 1);
            Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0]));
            Device->SetPixelShader(Reader);
            Results[Column] = Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Quad, sizeof(Quad[0]));
            Steps[Column] = Step;
            Column += 1;
        };

        Begin();
        Finish("nothing changed");
        Begin();
        Device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_FLAT);
        Finish("shade mode");
        Begin();
        Device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
        Device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_FLAT);
        Finish("shade mode there and back");
        Begin();
        Device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_FLAT);
        Finish("same shade mode");
        Begin();
        Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        Finish("blend state");
        Begin();
        Device->SetPixelShader(Defined);
        Finish("same shader");
        Begin();
        Device->SetRenderState(D3DRS_WRAP0, D3DWRAP_U);
        Finish("wrap");
        Begin();
        Device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        Device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
        Finish("alpha test");

        Begin();
        Device->SetRenderState(D3DRS_ADAPTIVETESS_Y, static_cast<DWORD>(MAKEFOURCC('A', 'T', 'O', 'C')));
        Finish("alpha to coverage");
        Begin();
        Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        Finish("blend state with coverage");
        Begin();
        Device->SetRenderState(D3DRS_ADAPTIVETESS_Y, 0);
        Finish("coverage off");
        Begin();
        Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        Finish("blend state after coverage");
        Begin();
        Device->SetRenderState(D3DRS_POINTSIZE, static_cast<DWORD>(MAKEFOURCC('A', '2', 'M', '1')));
        Finish("point size coverage");
        Begin();
        Device->SetRenderState(D3DRS_POINTSIZE, static_cast<DWORD>(MAKEFOURCC('A', '2', 'M', '0')));
        Device->SetRenderState(D3DRS_POINTSIZE, CkFloatBits(1.0f));
        Finish("point size coverage off");
        Begin();
        Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        Finish("alpha test off");
        Begin();
        Device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        Finish("alpha function with the test off");
    }
    Device->EndScene();
    if (CkDrawFlush != nullptr)
        CkDrainQuietly(Device, CkDrawFlush);
    CkRun(Cleared, "defines", "Clear");
    for (UINT Index = 0; Index < Column; Index += 1)
        CkRun(Results[Index], "defines", Steps[Index]);
    CkReadRows(Device, Back, "defines");

    Device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    Device->SetRenderState(D3DRS_WRAP0, 0);
    Device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
    Device->SetPixelShader(nullptr);

Done:
    if (Reader != nullptr)
        Reader->Release();
    if (Defined != nullptr)
        Defined->Release();
}

VOID
CkDraws(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back,
    _In_opt_ IDirect3DSurface9* Flush)
{
    printf("== draws\n");
    CkDrawFlush = Flush;
    CkPretransformed(Device, Back);
    CkShaderDraws(Device, Back);
    CkFixedFunction(Device, Back);
    CkIndexedFans(Device, Back);
    CkTextured(Device, Back);
    CkDepthStencilFog(Device, Back);
    CkPointSprites(Device, Back);
    CkDrawFlush = nullptr;
    CkFlushState();
}

VOID
CkPureDefines(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back,
    _In_opt_ IDirect3DSurface9* Flush)
{
    printf("== pure device defines\n");
    CkDrawFlush = Flush;
    CkDrawQuiet = TRUE;
    CkPixelDefines(Device, Back);
    CkDrawQuiet = FALSE;
    CkDrawFlush = nullptr;
    CkFlushState();
}
