/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared texture checks: create, share, open on another device or process, read back
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * sharecheck [-runtime system|reactx|<dir>] [check ...]
 * sharecheck -list
 * sharecheck [-runtime ...] -child kmt|nt|km|kmnt <handle>   opens a handle from the parent and reports its pixels;
 *                                                         km and kmnt take key 2 of its keyed mutex first
 */

#include <windows.h>
#include <winternl.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dkmthk.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "FullScreenVS.h"
#include "CopyPS.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

using Microsoft::WRL::ComPtr;

constexpr UINT ScWidth = 64;
constexpr UINT ScHeight = 64;
constexpr UINT ScPatchLeft = 8;
constexpr UINT ScPatchTop = 8;
constexpr UINT ScPatchSide = 16;
constexpr NTSTATUS ScStatusNotSupported = (NTSTATUS)0xC00000BB;

/* Where a keyed mutex texture's record keeps the global handles of its mutex and fence */
constexpr UINT ScRecordMutexHandles = 32;

/* What the creator clears to and what an opener clears to when it writes back */
static const FLOAT ScClearColor[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
static const FLOAT ScOpenerColor[4] = { 1.0f, 0.0f, 0.5f, 1.0f };

/* One runtime and the place facts go */
class ShareCheck
{
public:
    PFN_D3D11_CREATE_DEVICE CreateDevice = nullptr;
    std::wstring Runtime;
    PCSTR CheckName = "";
    UINT Failures = 0;

    VOID
    Report(
        _In_z_ PCSTR Key,
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...)
    {
        va_list Arguments;

        printf("%s.%s = ", CheckName, Key);
        va_start(Arguments, Format);
        vprintf(Format, Arguments);
        va_end(Arguments);
        printf("\n");
    }

    VOID
    ReportResult(
        _In_z_ PCSTR Key,
        _In_ HRESULT Result)
    {
        Report(Key, "0x%08lX", Result);
    }

    BOOLEAN
    Expect(
        _In_ BOOLEAN Condition,
        _In_z_ PCSTR What)
    {
        if (!Condition)
        {
            printf("FAIL %s: %s\n", CheckName, What);
            Failures++;
        }

        return Condition;
    }

    HRESULT
    MakeDevice(
        _COM_Outptr_ ID3D11Device** Device)
    {
        static const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        return CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, Levels, _countof(Levels), D3D11_SDK_VERSION,
                            Device, nullptr, nullptr);
    }
};

struct CheckInfo
{
    PCSTR Name;
    VOID (*Run)(ShareCheck& Check);
};

static
std::string
ScKey(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    char Text[128];
    va_list Arguments;

    va_start(Arguments, Format);
    vsnprintf(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);
    return Text;
}

/* The pixel the patch written by UpdateSubresource holds at one spot */
static
UINT32
ScPatchPixel(
    _In_ UINT X,
    _In_ UINT Y)
{
    return 0xFF000000 | ((X * 16) << 16) | ((Y * 16) << 8) | 0x5A;
}

static
HRESULT
ScCreateShared(
    _In_ ID3D11Device* Device,
    _In_ UINT MiscFlags,
    _COM_Outptr_ ID3D11Texture2D** Texture)
{
    D3D11_TEXTURE2D_DESC Desc = {};

    Desc.Width = ScWidth;
    Desc.Height = ScHeight;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Desc.MiscFlags = MiscFlags;
    return Device->CreateTexture2D(&Desc, nullptr, Texture);
}

/* Flushes and waits until the GPU is done, so another device sees finished contents */
static
HRESULT
ScFinish(
    _In_ ID3D11Device* Device)
{
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11Query> Query;
    D3D11_QUERY_DESC Desc = { D3D11_QUERY_EVENT, 0 };
    BOOL Done = FALSE;
    HRESULT hr;

    hr = Device->CreateQuery(&Desc, &Query);
    if (FAILED(hr))
        return hr;

    Device->GetImmediateContext(&Context);
    Context->End(Query.Get());
    Context->Flush();
    while ((hr = Context->GetData(Query.Get(), &Done, sizeof(Done), 0)) == S_FALSE)
        SwitchToThread();

    return FAILED(hr) ? hr : S_OK;
}

/* Clears the whole texture, writes a small patch through UpdateSubresource and flushes */
static
HRESULT
ScFill(
    _In_ ID3D11Device* Device,
    _In_ ID3D11Texture2D* Texture,
    _In_reads_(4) const FLOAT* Color)
{
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11RenderTargetView> View;
    UINT32 Patch[ScPatchSide * ScPatchSide];
    D3D11_BOX Box;
    UINT X;
    UINT Y;
    HRESULT hr;

    hr = Device->CreateRenderTargetView(Texture, nullptr, &View);
    if (FAILED(hr))
        return hr;

    for (Y = 0; Y < ScPatchSide; Y++)
    {
        for (X = 0; X < ScPatchSide; X++)
            Patch[Y * ScPatchSide + X] = ScPatchPixel(X, Y);
    }

    Box = { ScPatchLeft, ScPatchTop, 0, ScPatchLeft + ScPatchSide, ScPatchTop + ScPatchSide, 1 };
    Device->GetImmediateContext(&Context);
    Context->ClearRenderTargetView(View.Get(), Color);
    Context->UpdateSubresource(Texture, 0, &Box, Patch, ScPatchSide * sizeof(UINT32), 0);
    return ScFinish(Device);
}

/* Copies the texture into a staging twin on the same device and maps it */
static
HRESULT
ScRead(
    _In_ ID3D11Device* Device,
    _In_ ID3D11Texture2D* Texture,
    _Out_ std::vector<UINT32>& Pixels)
{
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11Texture2D> Staging;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    D3D11_TEXTURE2D_DESC Desc;
    UINT Row;
    HRESULT hr;

    Pixels.clear();
    Texture->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Staging);
    if (FAILED(hr))
        return hr;

    Device->GetImmediateContext(&Context);
    Context->CopyResource(Staging.Get(), Texture);
    hr = Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped);
    if (FAILED(hr))
        return hr;

    Pixels.resize(Desc.Width * Desc.Height);
    for (Row = 0; Row < Desc.Height; Row++)
    {
        memcpy(&Pixels[Row * Desc.Width], static_cast<BYTE*>(Mapped.pData) + Row * Mapped.RowPitch,
               Desc.Width * sizeof(UINT32));
    }

    Context->Unmap(Staging.Get(), 0);
    return S_OK;
}

/* Draws the texture into a fresh render target through a shader resource view, then reads that */
static
HRESULT
ScSample(
    _In_ ID3D11Device* Device,
    _In_ ID3D11Texture2D* Texture,
    _Out_ std::vector<UINT32>& Pixels)
{
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11ShaderResourceView> SourceView;
    ComPtr<ID3D11Texture2D> Target;
    ComPtr<ID3D11RenderTargetView> TargetView;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ID3D11ShaderResourceView* Views[1];
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_VIEWPORT Viewport = {};
    HRESULT hr;

    Pixels.clear();
    hr = Device->CreateShaderResourceView(Texture, nullptr, &SourceView);
    if (FAILED(hr))
        return hr;

    Desc.Width = ScWidth;
    Desc.Height = ScHeight;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Target);
    if (SUCCEEDED(hr))
        hr = Device->CreateRenderTargetView(Target.Get(), nullptr, &TargetView);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(g_FullScreenVS, sizeof(g_FullScreenVS), nullptr, &VertexShader);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(g_CopyPS, sizeof(g_CopyPS), nullptr, &PixelShader);
    if (FAILED(hr))
        return hr;

    Viewport.Width = (FLOAT)ScWidth;
    Viewport.Height = (FLOAT)ScHeight;
    Viewport.MaxDepth = 1.0f;
    Views[0] = SourceView.Get();

    Device->GetImmediateContext(&Context);
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->VSSetShader(VertexShader.Get(), nullptr, 0);
    Context->PSSetShader(PixelShader.Get(), nullptr, 0);
    Context->PSSetShaderResources(0, 1, Views);
    Context->RSSetViewports(1, &Viewport);
    Context->OMSetRenderTargets(1, TargetView.GetAddressOf(), nullptr);
    Context->Draw(3, 0);

    Views[0] = nullptr;
    Context->PSSetShaderResources(0, 1, Views);
    Context->OMSetRenderTargets(0, nullptr, nullptr);
    return ScRead(Device, Target.Get(), Pixels);
}

/* A hash of every pixel plus two that tell a clear from the patch */
static
std::string
ScSummary(
    _In_ const std::vector<UINT32>& Pixels)
{
    UINT32 Hash = 2166136261u;

    if (Pixels.size() != ScWidth * ScHeight)
        return "none";

    for (UINT32 Pixel : Pixels)
    {
        Hash ^= Pixel;
        Hash *= 16777619u;
    }

    return ScKey("hash %08X corner %08X patch %08X", Hash, Pixels[0],
                 Pixels[(ScPatchTop + 3) * ScWidth + ScPatchLeft + 5]);
}

static
VOID
ScReportPixels(
    _In_ ShareCheck& Check,
    _In_z_ PCSTR Key,
    _In_ HRESULT Result,
    _In_ const std::vector<UINT32>& Pixels)
{
    if (FAILED(Result))
        Check.Report(Key, "error 0x%08lX", Result);
    else
        Check.Report(Key, "%s", ScSummary(Pixels).c_str());
}

static
VOID
ScReportDesc(
    _In_ ShareCheck& Check,
    _In_z_ PCSTR Key,
    _In_ ID3D11Texture2D* Texture)
{
    D3D11_TEXTURE2D_DESC Desc;

    Texture->GetDesc(&Desc);
    Check.Report(Key, "%ux%u mips %u array %u format %u samples %u/%u usage %u bind 0x%X cpu 0x%X misc 0x%X",
                 Desc.Width, Desc.Height, Desc.MipLevels, Desc.ArraySize, Desc.Format, Desc.SampleDesc.Count,
                 Desc.SampleDesc.Quality, Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags, Desc.MiscFlags);
}

/* Which interfaces OpenSharedResource hands out for one handle */
static
VOID
ScReportOpenInterfaces(
    _In_ ShareCheck& Check,
    _In_ ID3D11Device* Device,
    _In_ HANDLE Handle,
    _In_ BOOLEAN IsNt)
{
    static const struct
    {
        PCSTR Name;
        IID Interface;
    } Interfaces[] =
    {
        { "ID3D11Resource", __uuidof(ID3D11Resource) },
        { "ID3D11Texture2D", __uuidof(ID3D11Texture2D) },
        { "ID3D11Texture1D", __uuidof(ID3D11Texture1D) },
        { "IDXGIResource", __uuidof(IDXGIResource) },
        { "IDXGIResource1", __uuidof(IDXGIResource1) },
        { "IDXGISurface", __uuidof(IDXGISurface) },
        { "IDXGISurface1", __uuidof(IDXGISurface1) },
        { "IUnknown", __uuidof(IUnknown) },
    };
    ComPtr<ID3D11Device1> Device1;
    ComPtr<IUnknown> Opened;
    UINT Index;
    HRESULT hr;

    Device->QueryInterface(IID_PPV_ARGS(&Device1));
    for (Index = 0; Index < _countof(Interfaces); Index++)
    {
        Opened.Reset();
        if (IsNt)
            hr = Device1 ? Device1->OpenSharedResource1(Handle, Interfaces[Index].Interface, &Opened) : E_NOINTERFACE;
        else
            hr = Device->OpenSharedResource(Handle, Interfaces[Index].Interface, &Opened);
        Check.ReportResult(ScKey("open_as_%s", Interfaces[Index].Name).c_str(), hr);
    }
}

/* Opens the kernel resource behind Handle on a kernel device of our own */
static
NTSTATUS
ScOpenKernelResource(
    _In_ D3DKMT_HANDLE KmDevice,
    _In_ HANDLE Handle,
    _In_ BOOLEAN IsNt,
    _Out_ std::vector<BYTE>& Record,
    _Out_ D3DKMT_HANDLE* KmResource)
{
    HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");
    auto QueryInfo = reinterpret_cast<PFND3DKMT_QUERYRESOURCEINFO>(GetProcAddress(Gdi32, "D3DKMTQueryResourceInfo"));
    auto OpenResource = reinterpret_cast<PFND3DKMT_OPENRESOURCE>(GetProcAddress(Gdi32, "D3DKMTOpenResource"));
    auto QueryInfoNt = reinterpret_cast<PFND3DKMT_QUERYRESOURCEINFOFROMNTHANDLE>(
        GetProcAddress(Gdi32, "D3DKMTQueryResourceInfoFromNtHandle"));
    auto OpenResourceNt = reinterpret_cast<PFND3DKMT_OPENRESOURCEFROMNTHANDLE>(
        GetProcAddress(Gdi32, "D3DKMTOpenResourceFromNtHandle"));
    D3DKMT_QUERYRESOURCEINFO Query = {};
    D3DKMT_OPENRESOURCE Open = {};
    D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE QueryNt = {};
    D3DKMT_OPENRESOURCEFROMNTHANDLE OpenNt = {};
    std::vector<D3DDDI_OPENALLOCATIONINFO> Allocations;
    std::vector<D3DDDI_OPENALLOCATIONINFO2> Allocations2;
    std::vector<BYTE> ResourceData;
    std::vector<BYTE> AllocationData;
    std::vector<BYTE> MutexData;
    NTSTATUS Status;

    Record.clear();
    *KmResource = 0;
    if (QueryInfo == nullptr || OpenResource == nullptr || QueryInfoNt == nullptr || OpenResourceNt == nullptr)
        return ScStatusNotSupported;

    if (IsNt)
    {
        QueryNt.hDevice = KmDevice;
        QueryNt.hNtHandle = Handle;
        Status = QueryInfoNt(&QueryNt);
        if (!NT_SUCCESS(Status))
            return Status;

        Record.resize(QueryNt.PrivateRuntimeDataSize);
        Allocations2.resize(QueryNt.NumAllocations);
        ResourceData.resize(QueryNt.ResourcePrivateDriverDataSize + 1);
        AllocationData.resize(QueryNt.TotalPrivateDriverDataSize + 1);
        MutexData.resize(sizeof(UINT));
        OpenNt.hDevice = KmDevice;
        OpenNt.hNtHandle = Handle;
        OpenNt.NumAllocations = QueryNt.NumAllocations;
        OpenNt.pOpenAllocationInfo2 = Allocations2.data();
        OpenNt.PrivateRuntimeDataSize = QueryNt.PrivateRuntimeDataSize;
        OpenNt.pPrivateRuntimeData = Record.data();
        OpenNt.ResourcePrivateDriverDataSize = QueryNt.ResourcePrivateDriverDataSize;
        OpenNt.pResourcePrivateDriverData = ResourceData.data();
        OpenNt.TotalPrivateDriverDataBufferSize = QueryNt.TotalPrivateDriverDataSize;
        OpenNt.pTotalPrivateDriverDataBuffer = AllocationData.data();
        OpenNt.pKeyedMutexPrivateRuntimeData = MutexData.data();
        OpenNt.KeyedMutexPrivateRuntimeDataSize = (UINT)MutexData.size();
        Status = OpenResourceNt(&OpenNt);
        *KmResource = OpenNt.hResource;
        return Status;
    }

    Query.hDevice = KmDevice;
    Query.hGlobalShare = (D3DKMT_HANDLE)(ULONG_PTR)Handle;
    Status = QueryInfo(&Query);
    if (!NT_SUCCESS(Status))
        return Status;

    Record.resize(Query.PrivateRuntimeDataSize);
    Allocations.resize(Query.NumAllocations);
    ResourceData.resize(Query.ResourcePrivateDriverDataSize + 1);
    AllocationData.resize(Query.TotalPrivateDriverDataSize + 1);
    Open.hDevice = KmDevice;
    Open.hGlobalShare = Query.hGlobalShare;
    Open.NumAllocations = Query.NumAllocations;
    Open.pOpenAllocationInfo = Allocations.data();
    Open.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Open.pPrivateRuntimeData = Record.data();
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pResourcePrivateDriverData = ResourceData.data();
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = AllocationData.data();
    Status = OpenResource(&Open);
    *KmResource = Open.hResource;
    return Status;
}

/* Reads the runtime record the creating runtime stored with the kernel resource */
static
std::string
ScRuntimeRecord(
    _In_ ID3D11Device* Device,
    _In_ HANDLE Handle,
    _In_ BOOLEAN IsNt,
    _In_ BOOLEAN HideMutexHandles = FALSE)
{
    HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");
    auto OpenAdapter =
        reinterpret_cast<PFND3DKMT_OPENADAPTERFROMLUID>(GetProcAddress(Gdi32, "D3DKMTOpenAdapterFromLuid"));
    auto CloseAdapter = reinterpret_cast<PFND3DKMT_CLOSEADAPTER>(GetProcAddress(Gdi32, "D3DKMTCloseAdapter"));
    auto CreateKmDevice = reinterpret_cast<PFND3DKMT_CREATEDEVICE>(GetProcAddress(Gdi32, "D3DKMTCreateDevice"));
    auto DestroyKmDevice = reinterpret_cast<PFND3DKMT_DESTROYDEVICE>(GetProcAddress(Gdi32, "D3DKMTDestroyDevice"));
    auto DestroyAllocation =
        reinterpret_cast<PFND3DKMT_DESTROYALLOCATION>(GetProcAddress(Gdi32, "D3DKMTDestroyAllocation"));
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Adapter;
    D3DKMT_OPENADAPTERFROMLUID Open = {};
    D3DKMT_CLOSEADAPTER Close = {};
    D3DKMT_CREATEDEVICE Create = {};
    D3DKMT_DESTROYDEVICE Destroy = {};
    D3DKMT_DESTROYALLOCATION Release = {};
    D3DKMT_HANDLE KmResource;
    DXGI_ADAPTER_DESC AdapterDesc;
    std::vector<BYTE> Record;
    std::string Text;
    NTSTATUS Status;
    SIZE_T Index;

    if (OpenAdapter == nullptr || CreateKmDevice == nullptr || DestroyAllocation == nullptr ||
        FAILED(Device->QueryInterface(IID_PPV_ARGS(&DxgiDevice))) || FAILED(DxgiDevice->GetAdapter(&Adapter)) ||
        FAILED(Adapter->GetDesc(&AdapterDesc)))
    {
        return "unavailable";
    }

    Open.AdapterLuid = AdapterDesc.AdapterLuid;
    if (!NT_SUCCESS(OpenAdapter(&Open)))
        return "no adapter";

    Create.hAdapter = Open.hAdapter;
    Status = CreateKmDevice(&Create);
    if (!NT_SUCCESS(Status))
    {
        Close.hAdapter = Open.hAdapter;
        CloseAdapter(&Close);
        return "no device";
    }

    Status = ScOpenKernelResource(Create.hDevice, Handle, IsNt, Record, &KmResource);
    if (!NT_SUCCESS(Status))
        Text = ScKey("open failed 0x%08lX", Status);
    else
        Text = ScKey("size 0x%X:", (UINT)Record.size());

    /* Mutex and fence handles change every run, so only whether they are there is compared */
    for (Index = 0; Index < Record.size(); Index++)
    {
        if (HideMutexHandles && Index >= ScRecordMutexHandles && Index < ScRecordMutexHandles + 8)
        {
            if ((Index - ScRecordMutexHandles) % 4 == 0)
                Text += (Index + 4 <= Record.size() && *reinterpret_cast<UINT32*>(&Record[Index]) != 0) ? " set" : " 0";
            continue;
        }

        Text += ScKey(" %02X", Record[Index]);
    }

    if (KmResource != 0)
    {
        Release.hDevice = Create.hDevice;
        Release.hResource = KmResource;
        DestroyAllocation(&Release);
    }

    Destroy.hDevice = Create.hDevice;
    DestroyKmDevice(&Destroy);
    Close.hAdapter = Open.hAdapter;
    CloseAdapter(&Close);
    return Text;
}

/* Children open NT handles for these kinds; the others get a global share handle */
static
BOOLEAN
ScIsNtKind(
    _In_z_ PCWSTR Kind)
{
    return wcscmp(Kind, L"nt") == 0 || wcscmp(Kind, L"kmnt") == 0;
}

/* Children of these kinds go through the texture's keyed mutex */
static
BOOLEAN
ScIsMutexKind(
    _In_z_ PCWSTR Kind)
{
    return wcscmp(Kind, L"km") == 0 || wcscmp(Kind, L"kmnt") == 0;
}

/* Runs this exe on another runtime to open Handle there; returns what it read */
static
std::string
ScRunChild(
    _In_z_ PCWSTR Runtime,
    _In_z_ PCWSTR Kind,
    _In_ HANDLE Handle)
{
    SECURITY_ATTRIBUTES Security = { sizeof(Security), nullptr, TRUE };
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process = {};
    WCHAR Module[MAX_PATH];
    HANDLE ReadPipe;
    HANDLE WritePipe;
    std::wstring Command;
    std::string Output;
    std::string Line;
    char Buffer[512];
    DWORD Read;
    size_t Found;

    if (!CreatePipe(&ReadPipe, &WritePipe, &Security, 0))
        return "no pipe";
    SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0);

    /* An NT handle travels by inheritance; a global share handle is just a number */
    if (ScIsNtKind(Kind))
        SetHandleInformation(Handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    GetModuleFileNameW(NULL, Module, _countof(Module));
    Command = L"\"" + std::wstring(Module) + L"\" -runtime " + Runtime + L" -child " + Kind + L" " +
              std::to_wstring((ULONG_PTR)Handle);
    Startup.dwFlags = STARTF_USESTDHANDLES;
    Startup.hStdOutput = WritePipe;
    Startup.hStdError = WritePipe;
    Startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    if (!CreateProcessW(nullptr, &Command[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &Startup, &Process))
    {
        CloseHandle(ReadPipe);
        CloseHandle(WritePipe);
        return "no process";
    }

    CloseHandle(WritePipe);
    while (ReadFile(ReadPipe, Buffer, sizeof(Buffer), &Read, nullptr) && Read != 0)
        Output.append(Buffer, Read);

    WaitForSingleObject(Process.hProcess, 30000);
    CloseHandle(Process.hProcess);
    CloseHandle(Process.hThread);
    CloseHandle(ReadPipe);
    if (ScIsNtKind(Kind))
        SetHandleInformation(Handle, HANDLE_FLAG_INHERIT, 0);

    Found = Output.find("child = ");
    if (Found == std::string::npos)
        return "no answer";

    Line = Output.substr(Found + 8);
    return Line.substr(0, Line.find_first_of("\r\n"));
}

/* kmt: MISC_SHARED and its global share handle */

static
VOID
ScCheckKmt(
    _In_ ShareCheck& Check)
{
    ComPtr<ID3D11Device> Creator;
    ComPtr<ID3D11Device> Opener;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11Texture2D> Opened;
    ComPtr<ID3D11Texture2D> Again;
    ComPtr<ID3D11ShaderResourceView> ShaderView;
    ComPtr<ID3D11RenderTargetView> TargetView;
    ComPtr<IDXGIResource> Resource;
    ComPtr<IDXGIResource1> Resource1;
    ComPtr<IDXGIResource> OpenedResource;
    ComPtr<IDXGIResource1> OpenedResource1;
    std::vector<UINT32> Pixels;
    HANDLE Handle = NULL;
    HANDLE OpenedHandle = NULL;
    HANDLE NtHandle = NULL;
    HRESULT hr;

    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(&Creator)), "creator device") ||
        !Check.Expect(SUCCEEDED(Check.MakeDevice(&Opener)), "opener device"))
    {
        return;
    }

    hr = ScCreateShared(Creator.Get(), D3D11_RESOURCE_MISC_SHARED, &Texture);
    Check.ReportResult("create", hr);
    if (FAILED(hr))
        return;

    ScReportDesc(Check, "creator_desc", Texture.Get());
    Texture.As(&Resource);
    Texture.As(&Resource1);
    hr = Resource->GetSharedHandle(&Handle);
    Check.ReportResult("get_shared_handle", hr);
    Check.Report("handle", "%s", Handle ? "set" : "null");
    hr = Resource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                       &NtHandle);
    Check.ReportResult("create_shared_handle", hr);
    if (NtHandle != NULL)
        CloseHandle(NtHandle);
    Check.Report("record", "%s", ScRuntimeRecord(Creator.Get(), Handle, FALSE).c_str());

    hr = ScFill(Creator.Get(), Texture.Get(), ScClearColor);
    Check.Expect(SUCCEEDED(hr), "fill");
    hr = ScRead(Creator.Get(), Texture.Get(), Pixels);
    ScReportPixels(Check, "creator_pixels", hr, Pixels);

    /* Same process, second device */
    hr = Opener->OpenSharedResource(Handle, IID_PPV_ARGS(&Opened));
    Check.ReportResult("open", hr);
    if (FAILED(hr))
        return;

    ScReportDesc(Check, "opened_desc", Opened.Get());
    hr = ScRead(Opener.Get(), Opened.Get(), Pixels);
    ScReportPixels(Check, "opened_pixels", hr, Pixels);
    hr = ScSample(Opener.Get(), Opened.Get(), Pixels);
    ScReportPixels(Check, "opened_sampled", hr, Pixels);

    Opened.As(&OpenedResource);
    hr = OpenedResource->GetSharedHandle(&OpenedHandle);
    Check.ReportResult("opened_get_shared_handle", hr);
    Check.Report("opened_handle", "%s", OpenedHandle == Handle ? "same" : (OpenedHandle ? "other" : "null"));
    Opened.As(&OpenedResource1);
    Check.ReportResult("opened_create_shared_handle",
                       OpenedResource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &NtHandle));
    if (NtHandle != NULL)
        CloseHandle(NtHandle);
    Check.ReportResult("opened_srv", Opener->CreateShaderResourceView(Opened.Get(), nullptr, &ShaderView));
    Check.ReportResult("opened_rtv", Opener->CreateRenderTargetView(Opened.Get(), nullptr, &TargetView));

    hr = Opener->OpenSharedResource(Handle, IID_PPV_ARGS(&Again));
    Check.ReportResult("open_again", hr);
    Check.Report("open_again_same", "%s", Again.Get() == Opened.Get() ? "yes" : "no");
    Again.Reset();
    hr = Creator->OpenSharedResource(Handle, IID_PPV_ARGS(&Again));
    Check.ReportResult("open_on_creator", hr);
    Check.Report("open_on_creator_same", "%s", Again.Get() == Texture.Get() ? "yes" : "no");
    Again.Reset();

    ScReportOpenInterfaces(Check, Opener.Get(), Handle, FALSE);

    /* Other processes on both runtimes */
    Check.Report("child_system", "%s", ScRunChild(L"system", L"kmt", Handle).c_str());
    Check.Report("child_reactx", "%s", ScRunChild(L"reactx", L"kmt", Handle).c_str());

    /* The opener writes, the creator reads */
    hr = ScFill(Opener.Get(), Opened.Get(), ScOpenerColor);
    Check.Expect(SUCCEEDED(hr), "opener fill");
    hr = ScRead(Creator.Get(), Texture.Get(), Pixels);
    ScReportPixels(Check, "written_back", hr, Pixels);
}

/* nt: MISC_SHARED_NTHANDLE, CreateSharedHandle and OpenSharedResource1 */

static
VOID
ScCheckNt(
    _In_ ShareCheck& Check)
{
    ComPtr<ID3D11Device> Creator;
    ComPtr<ID3D11Device> Opener;
    ComPtr<ID3D11Device1> Opener1;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11Texture2D> Alone;
    ComPtr<ID3D11Texture2D> Opened;
    ComPtr<ID3D11Texture2D> Wrong;
    ComPtr<IDXGIResource> Resource;
    ComPtr<IDXGIResource1> Resource1;
    ComPtr<IDXGIResource> OpenedResource;
    ComPtr<IDXGIResource1> OpenedResource1;
    std::vector<UINT32> Pixels;
    HANDLE Handle = (HANDLE)1;
    HANDLE NtHandle = NULL;
    HANDLE Again = NULL;
    HRESULT hr;

    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(&Creator)), "creator device") ||
        !Check.Expect(SUCCEEDED(Check.MakeDevice(&Opener)), "opener device") ||
        !Check.Expect(SUCCEEDED(Opener.As(&Opener1)), "ID3D11Device1"))
    {
        return;
    }

    hr = ScCreateShared(Creator.Get(), D3D11_RESOURCE_MISC_SHARED_NTHANDLE, &Alone);
    Check.ReportResult("create_nthandle_alone", hr);
    hr = ScCreateShared(Creator.Get(), D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE, &Texture);
    Check.ReportResult("create", hr);
    if (FAILED(hr))
        return;

    ScReportDesc(Check, "creator_desc", Texture.Get());
    Texture.As(&Resource);
    Texture.As(&Resource1);
    hr = Resource->GetSharedHandle(&Handle);
    Check.ReportResult("get_shared_handle", hr);
    Check.Report("get_shared_handle_out", "%s", Handle == (HANDLE)1 ? "untouched" : (Handle ? "set" : "null"));
    Check.ReportResult("create_shared_handle_null", Resource1->CreateSharedHandle(nullptr, 0, nullptr, nullptr));
    hr = Resource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                       &NtHandle);
    Check.ReportResult("create_shared_handle", hr);
    if (FAILED(hr))
        return;

    Check.Report("record", "%s", ScRuntimeRecord(Creator.Get(), NtHandle, TRUE).c_str());
    hr = ScFill(Creator.Get(), Texture.Get(), ScClearColor);
    Check.Expect(SUCCEEDED(hr), "fill");

    hr = Opener->OpenSharedResource(NtHandle, IID_PPV_ARGS(&Wrong));
    Check.ReportResult("open_with_kmt_api", hr);
    Wrong.Reset();

    hr = Opener1->OpenSharedResource1(NtHandle, IID_PPV_ARGS(&Opened));
    Check.ReportResult("open", hr);
    if (FAILED(hr))
    {
        CloseHandle(NtHandle);
        return;
    }

    ScReportDesc(Check, "opened_desc", Opened.Get());
    hr = ScRead(Opener.Get(), Opened.Get(), Pixels);
    ScReportPixels(Check, "opened_pixels", hr, Pixels);
    hr = ScSample(Opener.Get(), Opened.Get(), Pixels);
    ScReportPixels(Check, "opened_sampled", hr, Pixels);
    ScReportOpenInterfaces(Check, Opener.Get(), NtHandle, TRUE);

    Opened.As(&OpenedResource);
    Opened.As(&OpenedResource1);
    Handle = (HANDLE)1;
    Check.ReportResult("opened_get_shared_handle", OpenedResource->GetSharedHandle(&Handle));
    Check.Report("opened_get_shared_handle_out", "%s", Handle == (HANDLE)1 ? "untouched" : (Handle ? "set" : "null"));
    Check.ReportResult("opened_create_shared_handle",
                       OpenedResource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &Again));
    if (Again != NULL)
        CloseHandle(Again);

    Check.Report("child_system", "%s", ScRunChild(L"system", L"nt", NtHandle).c_str());
    Check.Report("child_reactx", "%s", ScRunChild(L"reactx", L"nt", NtHandle).c_str());

    hr = ScFill(Opener.Get(), Opened.Get(), ScOpenerColor);
    Check.Expect(SUCCEEDED(hr), "opener fill");
    hr = ScRead(Creator.Get(), Texture.Get(), Pixels);
    ScReportPixels(Check, "written_back", hr, Pixels);

    CloseHandle(NtHandle);
}

/* lifetime: the creator goes away while an opener still uses the texture */

static
VOID
ScCheckLifetime(
    _In_ ShareCheck& Check)
{
    ComPtr<ID3D11Device> Creator;
    ComPtr<ID3D11Device> Opener;
    ComPtr<ID3D11Device1> Opener1;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11Texture2D> Opened;
    ComPtr<ID3D11Texture2D> OpenedNt;
    ComPtr<IDXGIResource> Resource;
    ComPtr<IDXGIResource1> Resource1;
    std::vector<UINT32> Pixels;
    HANDLE Handle = NULL;
    HANDLE NtHandle = NULL;
    HRESULT hr;

    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(&Creator)), "creator device") ||
        !Check.Expect(SUCCEEDED(Check.MakeDevice(&Opener)), "opener device") ||
        !Check.Expect(SUCCEEDED(Opener.As(&Opener1)), "ID3D11Device1"))
    {
        return;
    }

    /* A global share handle dies with the last kernel reference */
    if (Check.Expect(SUCCEEDED(ScCreateShared(Creator.Get(), D3D11_RESOURCE_MISC_SHARED, &Texture)), "texture"))
    {
        Texture.As(&Resource);
        Resource->GetSharedHandle(&Handle);
        ScFill(Creator.Get(), Texture.Get(), ScClearColor);
        Check.ReportResult("open", Opener->OpenSharedResource(Handle, IID_PPV_ARGS(&Opened)));
        Resource.Reset();
        Texture.Reset();
    }

    /* The NT handle keeps the resource after the creator closes everything */
    if (Check.Expect(SUCCEEDED(ScCreateShared(Creator.Get(),
                                              D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE,
                                              &Texture)), "nt texture"))
    {
        Texture.As(&Resource1);
        Resource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                      &NtHandle);
        ScFill(Creator.Get(), Texture.Get(), ScOpenerColor);
        Resource1.Reset();
        Texture.Reset();
    }

    Creator.Reset();
    if (Opened != nullptr)
    {
        hr = ScRead(Opener.Get(), Opened.Get(), Pixels);
        ScReportPixels(Check, "after_creator_gone", hr, Pixels);
        Check.ReportResult("reopen_after_creator_gone",
                           Opener->OpenSharedResource(Handle, IID_PPV_ARGS(&Texture)));
        Texture.Reset();
    }

    if (NtHandle != NULL)
    {
        hr = Opener1->OpenSharedResource1(NtHandle, IID_PPV_ARGS(&OpenedNt));
        Check.ReportResult("open_nt_after_creator_gone", hr);
        CloseHandle(NtHandle);
        if (SUCCEEDED(hr))
        {
            hr = ScRead(Opener.Get(), OpenedNt.Get(), Pixels);
            ScReportPixels(Check, "nt_after_creator_gone", hr, Pixels);
        }
    }

    Opened.Reset();
    OpenedNt.Reset();
    Check.Report("opener_survives", "%s", Opener ? "yes" : "no");
}

/* errors: handles that do not name a shared texture */

static
VOID
ScCheckErrors(
    _In_ ShareCheck& Check)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11Device1> Device1;
    ComPtr<ID3D11Texture2D> Plain;
    ComPtr<ID3D11Texture2D> Opened;
    ComPtr<IDXGIResource> Resource;
    ComPtr<IDXGIResource1> Resource1;
    HANDLE Handle = (HANDLE)1;
    HANDLE NtHandle = (HANDLE)1;
    HANDLE Event;
    void* Unused = (void*)1;

    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(&Device)), "device") ||
        !Check.Expect(SUCCEEDED(Device.As(&Device1)), "ID3D11Device1"))
    {
        return;
    }

    Check.Expect(SUCCEEDED(ScCreateShared(Device.Get(), 0, &Plain)), "plain texture");
    Plain.As(&Resource);
    Plain.As(&Resource1);
    Check.ReportResult("plain_get_shared_handle", Resource->GetSharedHandle(&Handle));
    Check.Report("plain_handle", "%s", Handle == (HANDLE)1 ? "untouched" : (Handle ? "set" : "null"));
    Check.ReportResult("plain_create_shared_handle", Resource1->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                                                                    &NtHandle));
    Check.Report("plain_nt_handle", "%s", NtHandle == (HANDLE)1 ? "untouched" : (NtHandle ? "set" : "null"));

    Check.ReportResult("open_null", Device->OpenSharedResource(NULL, IID_PPV_ARGS(&Opened)));
    Check.ReportResult("open_bogus", Device->OpenSharedResource((HANDLE)(ULONG_PTR)0x40001234, IID_PPV_ARGS(&Opened)));
    Check.ReportResult("open_null_out", Device->OpenSharedResource((HANDLE)(ULONG_PTR)0x40001234,
                                                                   __uuidof(ID3D11Texture2D), nullptr));
    Check.ReportResult("open1_null", Device1->OpenSharedResource1(NULL, IID_PPV_ARGS(&Opened)));

    Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check.ReportResult("open1_event", Device1->OpenSharedResource1(Event, IID_PPV_ARGS(&Opened)));
    CloseHandle(Event);
    Check.ReportResult("open_unused_iid", Device->OpenSharedResource(NULL, __uuidof(ID3D11Device), &Unused));
}

/* shapes: shared textures with other formats, mips, arrays and binds keep their description */

static
VOID
ScCheckShapes(
    _In_ ShareCheck& Check)
{
    static const D3D11_TEXTURE2D_DESC Shapes[] =
    {
        { 32, 16, 0, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, D3D11_RESOURCE_MISC_SHARED },
        { 16, 16, 1, 3, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT,
          D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_SHARED },
        { 48, 48, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, { 1, 0 }, D3D11_USAGE_DEFAULT,
          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0,
          D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE },
        { 8, 8, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0,
          D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_GDI_COMPATIBLE },
        { 32, 32, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 4, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0,
          D3D11_RESOURCE_MISC_SHARED },
    };
    ComPtr<ID3D11Device> Creator;
    ComPtr<ID3D11Device> Opener;
    ComPtr<ID3D11Device1> Opener1;
    UINT Index;
    HRESULT hr;

    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(&Creator)), "creator device") ||
        !Check.Expect(SUCCEEDED(Check.MakeDevice(&Opener)), "opener device") ||
        !Check.Expect(SUCCEEDED(Opener.As(&Opener1)), "ID3D11Device1"))
    {
        return;
    }

    for (Index = 0; Index < _countof(Shapes); Index++)
    {
        BOOLEAN IsNt = (Shapes[Index].MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0;
        ComPtr<ID3D11Texture2D> Texture;
        ComPtr<ID3D11Texture2D> Opened;
        ComPtr<IDXGIResource1> Resource;
        HANDLE Handle = NULL;

        hr = Creator->CreateTexture2D(&Shapes[Index], nullptr, &Texture);
        Check.ReportResult(ScKey("%u_create", Index).c_str(), hr);
        if (FAILED(hr))
            continue;

        Texture.As(&Resource);
        if (IsNt)
            hr = Resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                              &Handle);
        else
            hr = Resource->GetSharedHandle(&Handle);
        Check.ReportResult(ScKey("%u_handle", Index).c_str(), hr);
        if (FAILED(hr))
            continue;

        Check.Report(ScKey("%u_record", Index).c_str(), "%s", ScRuntimeRecord(Creator.Get(), Handle, IsNt).c_str());
        if (IsNt)
            hr = Opener1->OpenSharedResource1(Handle, IID_PPV_ARGS(&Opened));
        else
            hr = Opener->OpenSharedResource(Handle, IID_PPV_ARGS(&Opened));
        Check.ReportResult(ScKey("%u_open", Index).c_str(), hr);
        if (SUCCEEDED(hr))
            ScReportDesc(Check, ScKey("%u_opened_desc", Index).c_str(), Opened.Get());
        if (IsNt)
            CloseHandle(Handle);
    }
}

/* keyedmutex: MISC_SHARED_KEYEDMUTEX, IDXGIKeyedMutex and the mutex following the shared handle */

/* The texture OBS makes for its NV12 output: acquire, release, then a key nobody released to */
static
VOID
ScCheckMutexBasics(
    _In_ ShareCheck& Check,
    _In_ ID3D11Device* Device)
{
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11Texture2D> Plain;
    ComPtr<IDXGIKeyedMutex> Mutex;
    ComPtr<IDXGIResource> Resource;
    ComPtr<ID3D11Device> Owner;
    ComPtr<IUnknown> FromTexture;
    ComPtr<IUnknown> FromMutex;
    D3D11_TEXTURE2D_DESC Desc = {};
    HANDLE Handle = NULL;
    ULONG Held;
    ULONG Alone;
    HRESULT hr;

    Check.ReportResult("create_with_shared",
                       ScCreateShared(Device, D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,
                                      &Plain));
    Plain.Reset();
    if (SUCCEEDED(ScCreateShared(Device, D3D11_RESOURCE_MISC_SHARED, &Plain)))
        Check.ReportResult("query_on_shared", Plain.As(&Mutex));
    Plain.Reset();
    if (SUCCEEDED(ScCreateShared(Device, 0, &Plain)))
        Check.ReportResult("query_on_unshared", Plain.As(&Mutex));

    Desc.Width = 1024;
    Desc.Height = 768;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_NV12;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Texture);
    Check.ReportResult("nv12_create", hr);
    if (FAILED(hr))
        return;

    hr = Texture.As(&Mutex);
    Check.ReportResult("nv12_query_mutex", hr);
    if (FAILED(hr))
        return;

    Check.ReportResult("nv12_acquire_0", Mutex->AcquireSync(0, INFINITE));
    Check.ReportResult("nv12_acquire_twice", Mutex->AcquireSync(0, 0));
    Check.ReportResult("nv12_release_0", Mutex->ReleaseSync(0));
    Check.ReportResult("nv12_release_unowned", Mutex->ReleaseSync(0));
    Check.ReportResult("nv12_acquire_1", Mutex->AcquireSync(1, 0));
    Check.ReportResult("nv12_acquire_0_again", Mutex->AcquireSync(0, 0));
    Check.ReportResult("nv12_release_0_again", Mutex->ReleaseSync(0));

    /* The mutex is another face of the texture: same identity, same reference count, same device */
    Texture.As(&FromTexture);
    Mutex.As(&FromMutex);
    Check.Report("nv12_identity", "%s", FromTexture.Get() == FromMutex.Get() ? "same" : "different");
    Held = Texture->AddRef();
    Texture->Release();
    Mutex.Reset();
    Alone = Texture->AddRef();
    Texture->Release();
    Check.Report("nv12_shares_refcount", "%s", Held == Alone + 1 ? "yes" : "no");
    Texture.As(&Mutex);
    hr = Mutex->GetDevice(IID_PPV_ARGS(&Owner));
    Check.ReportResult("nv12_get_device", hr);
    Check.Report("nv12_device", "%s", Owner.Get() == Device ? "same" : "other");

    Texture.As(&Resource);
    hr = Resource->GetSharedHandle(&Handle);
    Check.ReportResult("nv12_get_shared_handle", hr);
    Check.Report("nv12_handle", "%s", Handle ? "set" : "null");
}

/*
 * The creator writes under key 0 and hands over key 1, a second device reads and hands over key 2,
 * children on both runtimes take key 2 and give it back, and the creator reads the opener's writes.
 */
static
VOID
ScCheckMutexHandOff(
    _In_ ShareCheck& Check,
    _In_ ID3D11Device* Creator,
    _In_ ID3D11Device* Opener,
    _In_ BOOLEAN IsNt)
{
    ComPtr<ID3D11Device1> Opener1;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11Texture2D> Opened;
    ComPtr<IDXGIKeyedMutex> Mutex;
    ComPtr<IDXGIKeyedMutex> OpenedMutex;
    ComPtr<IDXGIResource> Resource;
    ComPtr<IDXGIResource1> Resource1;
    std::vector<UINT32> Pixels;
    PCWSTR Kind = IsNt ? L"kmnt" : L"km";
    HANDLE Handle = NULL;
    HRESULT hr;

    hr = ScCreateShared(Creator,
                        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | (IsNt ? D3D11_RESOURCE_MISC_SHARED_NTHANDLE : 0),
                        &Texture);
    Check.ReportResult("create", hr);
    if (FAILED(hr))
        return;

    Check.ReportResult("query_mutex", Texture.As(&Mutex));
    if (Mutex == nullptr)
        return;

    Texture.As(&Resource);
    Texture.As(&Resource1);
    if (IsNt)
        hr = Resource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                           &Handle);
    else
        hr = Resource->GetSharedHandle(&Handle);
    Check.ReportResult("handle", hr);
    if (FAILED(hr))
        return;

    Check.Report("record", "%s", ScRuntimeRecord(Creator, Handle, IsNt, TRUE).c_str());

    Check.ReportResult("creator_acquire", Mutex->AcquireSync(0, INFINITE));
    Check.Expect(SUCCEEDED(ScFill(Creator, Texture.Get(), ScClearColor)), "creator fill");
    Check.ReportResult("creator_release", Mutex->ReleaseSync(1));

    if (IsNt)
    {
        hr = Opener->QueryInterface(IID_PPV_ARGS(&Opener1));
        if (SUCCEEDED(hr))
            hr = Opener1->OpenSharedResource1(Handle, IID_PPV_ARGS(&Opened));
    }
    else
    {
        hr = Opener->OpenSharedResource(Handle, IID_PPV_ARGS(&Opened));
    }
    Check.ReportResult("open", hr);
    if (SUCCEEDED(hr))
        Check.ReportResult("opened_query_mutex", Opened.As(&OpenedMutex));

    if (OpenedMutex != nullptr)
    {
        Check.ReportResult("opener_acquire_0", OpenedMutex->AcquireSync(0, 0));
        Check.ReportResult("opener_acquire_1", OpenedMutex->AcquireSync(1, 5000));
        hr = ScRead(Opener, Opened.Get(), Pixels);
        ScReportPixels(Check, "opener_pixels", hr, Pixels);
        Check.Expect(SUCCEEDED(ScFill(Opener, Opened.Get(), ScOpenerColor)), "opener fill");
        Check.ReportResult("opener_release", OpenedMutex->ReleaseSync(2));
    }

    Check.Report("child_system", "%s", ScRunChild(L"system", Kind, Handle).c_str());
    Check.Report("child_reactx", "%s", ScRunChild(L"reactx", Kind, Handle).c_str());

    Check.ReportResult("creator_acquire_2", Mutex->AcquireSync(2, 5000));
    hr = ScRead(Creator, Texture.Get(), Pixels);
    ScReportPixels(Check, "written_back", hr, Pixels);
    Check.ReportResult("creator_release_0", Mutex->ReleaseSync(0));

    if (IsNt)
        CloseHandle(Handle);
}

static
VOID
ScCheckKeyedMutex(
    _In_ ShareCheck& Check)
{
    ComPtr<ID3D11Device> Creator;
    ComPtr<ID3D11Device> Opener;
    PCSTR Name = Check.CheckName;

    if (!Check.Expect(SUCCEEDED(Check.MakeDevice(&Creator)), "creator device") ||
        !Check.Expect(SUCCEEDED(Check.MakeDevice(&Opener)), "opener device"))
    {
        return;
    }

    ScCheckMutexBasics(Check, Creator.Get());

    Check.CheckName = "keyedmutex.kmt";
    ScCheckMutexHandOff(Check, Creator.Get(), Opener.Get(), FALSE);
    Check.CheckName = "keyedmutex.nt";
    ScCheckMutexHandOff(Check, Creator.Get(), Opener.Get(), TRUE);
    Check.CheckName = Name;
}

static const CheckInfo Checks[] =
{
    { "kmt", ScCheckKmt },
    { "nt", ScCheckNt },
    { "lifetime", ScCheckLifetime },
    { "errors", ScCheckErrors },
    { "shapes", ScCheckShapes },
    { "keyedmutex", ScCheckKeyedMutex },
};

/* System32 for "system", the ReactX build next to this exe for "reactx", else a directory */
static
std::wstring
ScRuntimeDirectory(
    _In_z_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\";
    }

    return std::wstring(Runtime) + L"\\";
}

/* The child side of ScRunChild: open, read, print one line */
static
int
ScChild(
    _In_ ShareCheck& Check,
    _In_z_ PCWSTR Kind,
    _In_ HANDLE Handle)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11Device1> Device1;
    ComPtr<ID3D11Texture2D> Opened;
    ComPtr<IDXGIKeyedMutex> Mutex;
    std::vector<UINT32> Pixels;
    HRESULT Released;
    HRESULT hr;

    hr = Check.MakeDevice(&Device);
    if (SUCCEEDED(hr) && ScIsNtKind(Kind))
    {
        hr = Device.As(&Device1);
        if (SUCCEEDED(hr))
            hr = Device1->OpenSharedResource1(Handle, IID_PPV_ARGS(&Opened));
    }
    else if (SUCCEEDED(hr))
    {
        hr = Device->OpenSharedResource(Handle, IID_PPV_ARGS(&Opened));
    }

    /* Keyed mutex children wait for key 2, read, and hand key 2 back */
    if (SUCCEEDED(hr) && ScIsMutexKind(Kind))
    {
        hr = Opened.As(&Mutex);
        if (SUCCEEDED(hr))
            hr = Mutex->AcquireSync(2, 5000);
        if (hr != S_OK)
        {
            printf("child = acquire 0x%08lX\n", hr);
            return 1;
        }
    }

    if (SUCCEEDED(hr))
        hr = ScRead(Device.Get(), Opened.Get(), Pixels);

    if (Mutex != nullptr)
    {
        Released = Mutex->ReleaseSync(2);
        printf("child = %s release 0x%08lX\n", SUCCEEDED(hr) ? ScSummary(Pixels).c_str() : "error", Released);
        return (SUCCEEDED(hr) && SUCCEEDED(Released)) ? 0 : 1;
    }

    if (SUCCEEDED(hr))
        printf("child = %s\n", ScSummary(Pixels).c_str());
    else
        printf("child = error 0x%08lX\n", hr);

    return SUCCEEDED(hr) ? 0 : 1;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::vector<std::string> Wanted;
    PCWSTR Runtime = L"system";
    PCWSTR ChildKind = nullptr;
    HANDLE ChildHandle = NULL;
    std::wstring Directory;
    ShareCheck Check;
    HMODULE D3D11;
    UINT Failures = 0;
    int Index;
    char Name[128];

    setvbuf(stdout, nullptr, _IONBF, 0);

    for (Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-list") == 0)
        {
            for (const CheckInfo& Info : Checks)
                printf("%s\n", Info.Name);
            return 0;
        }

        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = argv[++Index];
            continue;
        }

        if (_wcsicmp(argv[Index], L"-child") == 0 && Index + 2 < argc)
        {
            ChildKind = argv[++Index];
            ChildHandle = (HANDLE)(ULONG_PTR)_wcstoui64(argv[++Index], nullptr, 10);
            continue;
        }

        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    /* dxgi first, so the d3d11 of the same pair finds it already loaded */
    Directory = ScRuntimeDirectory(Runtime);
    LoadLibraryW((Directory + L"dxgi.dll").c_str());
    D3D11 = LoadLibraryW((Directory + L"d3d11.dll").c_str());
    if (D3D11 == NULL)
    {
        fprintf(stderr, "error: cannot load d3d11.dll\n");
        return 1;
    }

    Check.Runtime = Runtime;
    Check.CreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(D3D11, "D3D11CreateDevice"));
    if (Check.CreateDevice == nullptr)
    {
        fprintf(stderr, "error: d3d11.dll has no D3D11CreateDevice\n");
        return 1;
    }

    if (ChildKind != nullptr)
        return ScChild(Check, ChildKind, ChildHandle);

    for (const CheckInfo& Info : Checks)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Each : Wanted)
            IsWanted = IsWanted || Each == Info.Name;
        if (!IsWanted)
            continue;

        Check.CheckName = Info.Name;
        Check.Failures = 0;
        Info.Run(Check);
        Failures += Check.Failures;
    }

    printf("failures = %u\n", Failures);
    return Failures ? 1 : 0;
}
