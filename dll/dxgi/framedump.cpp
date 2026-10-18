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

VOID
RxDumpFrame(
    _In_ IUnknown* Buffer,
    _In_ UINT PresentNumber)
{
    ID3D11Texture2D* Source = nullptr;
    ID3D11Texture2D* Staging = nullptr;
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* Context = nullptr;
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    BITMAPFILEHEADER FileHeader = {};
    BITMAPINFOHEADER InfoHeader = {};
    std::vector<UINT> Row;
    WCHAR Path[MAX_PATH];
    HANDLE File;
    DWORD Written;
    BOOLEAN Wanted = FALSE;
    UINT Y;

    InitOnceExecuteOnce(&DumpOnce, RxReadDumpSettings, nullptr, nullptr);
    for (UINT Frame : DumpFrames)
        Wanted |= Frame == PresentNumber;
    if (!Wanted || FAILED(Buffer->QueryInterface(IID_PPV_ARGS(&Source))))
        return;

    Source->GetDevice(&Device);
    Device->GetImmediateContext(&Context);
    Source->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.SampleDesc = { 1, 0 };

    if (SUCCEEDED(Device->CreateTexture2D(&Desc, nullptr, &Staging)))
    {
        Context->CopySubresourceRegion(Staging, 0, 0, 0, 0, Source, 0, nullptr);
        if (SUCCEEDED(Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped)))
        {
            swprintf_s(Path, L"%sreactx-frame-%u.bmp", DumpDirectory, PresentNumber);
            File = CreateFileW(Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (File != INVALID_HANDLE_VALUE)
            {
                InfoHeader.biSize = sizeof(InfoHeader);
                InfoHeader.biWidth = Desc.Width;
                InfoHeader.biHeight = -(LONG)Desc.Height;
                InfoHeader.biPlanes = 1;
                InfoHeader.biBitCount = 32;
                FileHeader.bfType = 0x4D42;
                FileHeader.bfOffBits = sizeof(FileHeader) + sizeof(InfoHeader);
                FileHeader.bfSize = FileHeader.bfOffBits + Desc.Width * Desc.Height * 4;
                WriteFile(File, &FileHeader, sizeof(FileHeader), &Written, nullptr);
                WriteFile(File, &InfoHeader, sizeof(InfoHeader), &Written, nullptr);

                Row.resize(Desc.Width);
                for (Y = 0; Y < Desc.Height; Y++)
                {
                    const BYTE* Line = static_cast<const BYTE*>(Mapped.pData) + (SIZE_T)Y * Mapped.RowPitch;

                    if (!RxConvertRowToBgra(Desc.Format, Line, Row.data(), Desc.Width))
                        memcpy(Row.data(), Line, Desc.Width * 4);
                    WriteFile(File, Row.data(), Desc.Width * 4, &Written, nullptr);
                }

                CloseHandle(File);
                RX_TRACE(L"dumped present %u (%ux%u format %u)", PresentNumber, Desc.Width, Desc.Height, Desc.Format);
            }

            Context->Unmap(Staging, 0);
        }

        Staging->Release();
    }

    Context->Release();
    Device->Release();
    Source->Release();
}
