/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Debug aid: writes chosen presented frames to BMP files
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "swapchain.hpp"

#include <d3d11.h>
#include <vector>

/* Present numbers to dump, from dumpframes= in reactx.ini, and where the files go */
static INIT_ONCE DumpOnce = INIT_ONCE_STATIC_INIT;
static std::vector<UINT> DumpFrames;
static WCHAR DumpDirectory[MAX_PATH];

static
BOOL
CALLBACK
RxReadDumpSettings(
    _Inout_ PINIT_ONCE InitOnce,
    _Inout_opt_ PVOID Parameter,
    _Out_opt_ PVOID* Context)
{
    WCHAR Settings[MAX_PATH];
    WCHAR List[512] = {};
    HMODULE Module = NULL;
    PWSTR Cursor;
    PWSTR Slash;

    UNREFERENCED_PARAMETER(InitOnce);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&RxReadDumpSettings), &Module) ||
        GetModuleFileNameW(Module, DumpDirectory, _countof(DumpDirectory)) == 0)
    {
        return TRUE;
    }

    Slash = wcsrchr(DumpDirectory, L'\\');
    if (Slash == nullptr)
        return TRUE;
    Slash[1] = UNICODE_NULL;

    swprintf_s(Settings, L"%sreactx.ini", DumpDirectory);
    GetPrivateProfileStringW(L"reactx", L"dumpframes", L"", List, _countof(List), Settings);
    for (Cursor = List; *Cursor != UNICODE_NULL;)
    {
        UINT Frame = wcstoul(Cursor, &Cursor, 10);

        if (Frame != 0)
            DumpFrames.push_back(Frame);
        while (*Cursor == L',' || *Cursor == L' ')
            Cursor++;
        if (*Cursor != UNICODE_NULL && (*Cursor < L'0' || *Cursor > L'9'))
            break;
    }

    return TRUE;
}

/* One row of a mapped texture as BGRA; formats the converter does not know are copied raw */
static
VOID
RxReadRow(
    _In_ const D3D11_MAPPED_SUBRESOURCE& Mapped,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Width,
    _In_ UINT Y,
    _Out_writes_(Width) UINT* Row)
{
    const BYTE* Line = static_cast<const BYTE*>(Mapped.pData) + (SIZE_T)Y * Mapped.RowPitch;

    if (!RxConvertRowToBgra(Format, Line, Row, Width))
        memcpy(Row, Line, Width * sizeof(*Row));
}

/* The middle pixel of a mapped texture, and how many of its pixels are not black */
static
UINT
RxSampleMapped(
    _In_ const D3D11_MAPPED_SUBRESOURCE& Mapped,
    _In_ const D3D11_TEXTURE2D_DESC& Desc,
    _Out_ UINT* Lit)
{
    std::vector<UINT> Row(Desc.Width);
    UINT Center = 0;
    UINT X;
    UINT Y;

    *Lit = 0;
    for (Y = 0; Y < Desc.Height; Y++)
    {
        RxReadRow(Mapped, Desc.Format, Desc.Width, Y, Row.data());
        for (X = 0; X < Desc.Width; X++)
            *Lit += (Row[X] & 0x00FFFFFF) != 0;
        if (Y == Desc.Height / 2)
            Center = Row[Desc.Width / 2];
    }

    return Center;
}

/* Creates reactx-frame-<n>.bmp with its headers written; the caller writes the rows, top first */
static
HANDLE
RxCreateBitmap(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT PresentNumber)
{
    BITMAPFILEHEADER FileHeader = {};
    BITMAPINFOHEADER InfoHeader = {};
    WCHAR Path[MAX_PATH];
    HANDLE File;
    DWORD Written;

    swprintf_s(Path, L"%sreactx-frame-%u.bmp", DumpDirectory, PresentNumber);
    File = CreateFileW(Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File == INVALID_HANDLE_VALUE)
        return File;

    InfoHeader.biSize = sizeof(InfoHeader);
    InfoHeader.biWidth = Width;
    InfoHeader.biHeight = -(LONG)Height;
    InfoHeader.biPlanes = 1;
    InfoHeader.biBitCount = 32;
    FileHeader.bfType = 0x4D42;
    FileHeader.bfOffBits = sizeof(FileHeader) + sizeof(InfoHeader);
    FileHeader.bfSize = FileHeader.bfOffBits + Width * Height * 4;
    WriteFile(File, &FileHeader, sizeof(FileHeader), &Written, nullptr);
    WriteFile(File, &InfoHeader, sizeof(InfoHeader), &Written, nullptr);
    return File;
}

static
VOID
RxWriteBitmap(
    _In_ const D3D11_MAPPED_SUBRESOURCE& Mapped,
    _In_ const D3D11_TEXTURE2D_DESC& Desc,
    _In_ UINT PresentNumber)
{
    std::vector<UINT> Row(Desc.Width);
    HANDLE File;
    DWORD Written;
    UINT Y;

    File = RxCreateBitmap(Desc.Width, Desc.Height, PresentNumber);
    if (File == INVALID_HANDLE_VALUE)
        return;

    for (Y = 0; Y < Desc.Height; Y++)
    {
        RxReadRow(Mapped, Desc.Format, Desc.Width, Y, Row.data());
        WriteFile(File, Row.data(), Desc.Width * 4, &Written, nullptr);
    }

    CloseHandle(File);
    RX_TRACE(L"dumped present %u (%ux%u format %u)", PresentNumber, Desc.Width, Desc.Height, Desc.Format);
}

/* What the kernel thinks of the device the buffer lives on */
static
VOID
RxTraceDeviceState(
    _In_ ID3D11Device* Device,
    _In_ UINT PresentNumber)
{
    D3DKMT_GETDEVICESTATE State = {};
    IRxPresentDevice* Present;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    if (SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(&Present))))
    {
        State.hDevice = Present->GetKernelDevice();
        Present->Release();
    }

    State.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    if (State.hDevice != 0 && RxGetKmtThunks()->GetDeviceState != nullptr)
        Status = RxGetKmtThunks()->GetDeviceState(&State);

    RX_TRACE(L"dump %u: kernel device 0x%X execution state %u (0x%08lX), removed reason 0x%08lX", PresentNumber,
             State.hDevice, State.ExecutionState, Status, Device->GetDeviceRemovedReason());
}

/* Waits up to two seconds for the GPU to pass everything queued so far */
static
VOID
RxTraceGpuIdle(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ UINT PresentNumber)
{
    D3D11_QUERY_DESC QueryDesc = { D3D11_QUERY_EVENT, 0 };
    ID3D11Query* Query;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;
    LARGE_INTEGER Now;
    BOOL Done = FALSE;
    LONGLONG Elapsed;
    HRESULT hr;

    hr = Device->CreateQuery(&QueryDesc, &Query);
    if (FAILED(hr))
    {
        RX_TRACE(L"dump %u: event query could not be made 0x%08lX", PresentNumber, hr);
        return;
    }

    Context->End(Query);
    Context->Flush();

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    for (;;)
    {
        hr = Context->GetData(Query, &Done, sizeof(Done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        QueryPerformanceCounter(&Now);
        Elapsed = (Now.QuadPart - Start.QuadPart) * 1000 / Frequency.QuadPart;
        if (hr != S_FALSE || Elapsed >= 2000)
            break;
        Sleep(1);
    }

    RX_TRACE(L"dump %u: event query 0x%08lX done %d after %lld ms", PresentNumber, hr, Done, Elapsed);
    Query->Release();
}

/* A render target the device clears to magenta, a known result to read back next to the frame */
static
HRESULT
RxPrepareScratch(
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _Outptr_result_maybenull_ ID3D11Texture2D** Readback)
{
    static const FLOAT Magenta[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    D3D11_TEXTURE2D_DESC Desc = {};
    ID3D11Texture2D* Target = nullptr;
    ID3D11RenderTargetView* View = nullptr;
    HRESULT hr;

    *Readback = nullptr;

    Desc.Width = 16;
    Desc.Height = 16;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc = { 1, 0 };
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    hr = Device->CreateTexture2D(&Desc, nullptr, &Target);
    if (SUCCEEDED(hr))
        hr = Device->CreateRenderTargetView(Target, nullptr, &View);

    if (SUCCEEDED(hr))
    {
        Desc.Usage = D3D11_USAGE_STAGING;
        Desc.BindFlags = 0;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = Device->CreateTexture2D(&Desc, nullptr, Readback);
    }

    if (SUCCEEDED(hr))
    {
        Context->ClearRenderTargetView(View, Magenta);
        Context->CopyResource(*Readback, Target);
    }

    if (View != nullptr)
        View->Release();
    if (Target != nullptr)
        Target->Release();
    return hr;
}

/* The scratch target's middle pixel, which reads 0xFFFF00FF when the device really renders */
static
UINT
RxReadScratch(
    _In_ ID3D11DeviceContext* Context,
    _In_ ID3D11Texture2D* Scratch,
    _Out_ HRESULT* Result)
{
    D3D11_MAPPED_SUBRESOURCE Mapped;
    D3D11_TEXTURE2D_DESC Desc;
    UINT Center = 0;
    UINT Lit;

    Scratch->GetDesc(&Desc);
    *Result = Context->Map(Scratch, 0, D3D11_MAP_READ, 0, &Mapped);
    if (SUCCEEDED(*Result))
    {
        Center = RxSampleMapped(Mapped, Desc, &Lit);
        Context->Unmap(Scratch, 0);
    }

    return Center;
}

VOID
RxDumpFrame(
    _In_ IUnknown* Buffer,
    _In_ UINT PresentNumber)
{
    ID3D11Texture2D* Source = nullptr;
    ID3D11Texture2D* Resolved = nullptr;
    ID3D11Texture2D* Staging = nullptr;
    ID3D11Texture2D* Scratch = nullptr;
    ID3D11Resource* CopySource;
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* Context = nullptr;
    D3D11_TEXTURE2D_DESC SourceDesc;
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    BOOLEAN Wanted = FALSE;
    UINT ScratchCenter = 0;
    UINT Center;
    UINT Lit;
    HRESULT ScratchHr;
    HRESULT hr;

    InitOnceExecuteOnce(&DumpOnce, RxReadDumpSettings, nullptr, nullptr);
    for (UINT Frame : DumpFrames)
        Wanted |= Frame == PresentNumber;
    if (!Wanted || FAILED(Buffer->QueryInterface(IID_PPV_ARGS(&Source))))
        return;

    Source->GetDevice(&Device);
    Device->GetImmediateContext(&Context);
    Source->GetDesc(&SourceDesc);
    RxTraceDeviceState(Device, PresentNumber);

    Desc = SourceDesc;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.SampleDesc = { 1, 0 };
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = 0;
    Desc.MiscFlags = 0;

    /* A multisampled buffer can not be copied to staging directly, so it is resolved first */
    CopySource = Source;
    if (SourceDesc.SampleDesc.Count > 1)
    {
        hr = Device->CreateTexture2D(&Desc, nullptr, &Resolved);
        RX_TRACE(L"dump %u: resolving %u samples 0x%08lX", PresentNumber, SourceDesc.SampleDesc.Count, hr);
        if (FAILED(hr))
            goto Done;

        Context->ResolveSubresource(Resolved, 0, Source, 0, SourceDesc.Format);
        CopySource = Resolved;
    }

    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Staging);
    if (FAILED(hr))
    {
        RX_TRACE(L"dump %u: staging texture could not be made 0x%08lX", PresentNumber, hr);
        goto Done;
    }

    Context->CopySubresourceRegion(Staging, 0, 0, 0, 0, CopySource, 0, nullptr);
    ScratchHr = RxPrepareScratch(Device, Context, &Scratch);
    RxTraceGpuIdle(Device, Context, PresentNumber);

    /* Read now and again after a pause, which tells a late GPU from a frame that is really black */
    hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        Center = RxSampleMapped(Mapped, Desc, &Lit);
        Context->Unmap(Staging, 0);

        if (Scratch != nullptr)
            ScratchCenter = RxReadScratch(Context, Scratch, &ScratchHr);

        RX_TRACE(L"dump %u: first read center 0x%08X, %u of %u lit; magenta scratch center 0x%08X (0x%08lX)",
                 PresentNumber, Center, Lit, Desc.Width * Desc.Height, ScratchCenter, ScratchHr);
    }
    else
    {
        RX_TRACE(L"dump %u: first map failed 0x%08lX", PresentNumber, hr);
    }

    Sleep(500);

    hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        Center = RxSampleMapped(Mapped, Desc, &Lit);
        RX_TRACE(L"dump %u: second read center 0x%08X, %u of %u lit", PresentNumber, Center, Lit,
                 Desc.Width * Desc.Height);
        RxWriteBitmap(Mapped, Desc, PresentNumber);
        Context->Unmap(Staging, 0);
    }
    else
    {
        RX_TRACE(L"dump %u: second map failed 0x%08lX", PresentNumber, hr);
    }

Done:
    if (Scratch != nullptr)
        Scratch->Release();
    if (Staging != nullptr)
        Staging->Release();
    if (Resolved != nullptr)
        Resolved->Release();
    Context->Release();
    Device->Release();
    Source->Release();
}

VOID
RxDumpFrame12(
    _In_ IUnknown* Queue,
    _In_ IUnknown* Buffer,
    _In_ UINT PresentNumber)
{
    std::vector<UINT> Pixels;
    BOOLEAN Wanted = FALSE;
    HANDLE File;
    DWORD Written;
    UINT Width;
    UINT Height;
    UINT Lit = 0;
    HRESULT hr;

    InitOnceExecuteOnce(&DumpOnce, RxReadDumpSettings, nullptr, nullptr);
    for (UINT Frame : DumpFrames)
        Wanted |= Frame == PresentNumber;
    if (!Wanted)
        return;

    /* The copy runs on the queue after the frame's own work, so it sees the finished frame */
    hr = RxReadBuffer12(Queue, Buffer, &Pixels, &Width, &Height);
    if (FAILED(hr))
    {
        RX_TRACE(L"dump %u: Direct3D 12 buffer could not be read 0x%08lX", PresentNumber, hr);
        return;
    }

    File = RxCreateBitmap(Width, Height, PresentNumber);
    if (File == INVALID_HANDLE_VALUE)
        return;

    WriteFile(File, Pixels.data(), Width * Height * 4, &Written, nullptr);
    CloseHandle(File);

    for (UINT Pixel : Pixels)
        Lit += (Pixel & 0x00FFFFFF) != 0;
    RX_TRACE(L"dumped Direct3D 12 present %u (%ux%u, center 0x%08X, %u lit)", PresentNumber, Width, Height,
             Pixels[(SIZE_T)(Height / 2) * Width + Width / 2], Lit);
}
