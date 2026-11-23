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

VOID
CkDraws(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back)
{
    printf("== draws\n");
    CkPretransformed(Device, Back);
    CkFlushState();
}
