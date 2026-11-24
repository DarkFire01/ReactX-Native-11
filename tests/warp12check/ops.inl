/*
 * PROJECT:     ReactX Basic tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader operation scenes: each program runs through dxilconv converted DXBC and native DXIL
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <dxcapi.h>

/* DXC from the Windows Kits, loaded by the test alone; WARP12CHECK_DXC names another copy */
static
HRESULT
TcDxcCompile(
    _In_z_ PCSTR Source,
    _In_z_ PCWSTR Entry,
    _In_z_ PCWSTR Profile,
    _In_opt_z_ PCWSTR Extra,
    _In_opt_z_ PCWSTR Define,
    _Out_ ID3DBlob **Blob)
{
    static HMODULE Module;
    static DxcCreateInstanceProc Create;
    IDxcCompiler3 *Compiler = NULL;
    IDxcResult *Outcome = NULL;
    IDxcBlob *Object = NULL;
    IDxcBlobUtf8 *Errors = NULL;
    DxcBuffer Buffer = { Source, strlen(Source), DXC_CP_UTF8 };
    LPCWSTR Arguments[8] = { L"-E", Entry, L"-T", Profile };
    UINT ArgumentCount = 4;
    HRESULT Status = E_FAIL;
    HRESULT hr;

    *Blob = NULL;
    if (Module == NULL)
    {
        WCHAR Path[MAX_PATH];
        DWORD Length = GetEnvironmentVariableW(L"WARP12CHECK_DXC", Path, ARRAYSIZE(Path));

        if (Length == 0 || Length >= ARRAYSIZE(Path))
            wcscpy_s(Path, L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.28000.0\\arm64\\dxcompiler.dll");
        Module = LoadLibraryExW(Path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (Module != NULL)
            Create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(Module, "DxcCreateInstance"));
    }
    if (Create == NULL)
    {
        printf("dxcompiler.dll did not load\n");
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    }
    if (Extra != NULL)
        Arguments[ArgumentCount++] = Extra;
    if (Define != NULL)
    {
        Arguments[ArgumentCount++] = L"-D";
        Arguments[ArgumentCount++] = Define;
    }

    hr = Create(CLSID_DxcCompiler, IID_PPV_ARGS(&Compiler));
    if (SUCCEEDED(hr))
        hr = Compiler->Compile(&Buffer, Arguments, ArgumentCount, NULL, IID_PPV_ARGS(&Outcome));
    if (SUCCEEDED(hr))
        hr = Outcome->GetStatus(&Status);
    if (SUCCEEDED(hr) && FAILED(Status))
    {
        if (SUCCEEDED(Outcome->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&Errors), NULL)) && Errors != NULL)
            printf("%s", Errors->GetStringPointer());
        hr = Status;
    }
    if (SUCCEEDED(hr))
        hr = Outcome->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&Object), NULL);
    if (SUCCEEDED(hr))
        hr = D3DCreateBlob(Object->GetBufferSize(), Blob);
    if (SUCCEEDED(hr))
        memcpy((*Blob)->GetBufferPointer(), Object->GetBufferPointer(), Object->GetBufferSize());
    if (Errors)
        Errors->Release();
    if (Object)
        Object->Release();
    if (Outcome)
        Outcome->Release();
    if (Compiler)
        Compiler->Release();
    return hr;
}

/* One way to build a program: FXC bytecode the runtime converts, or DXC's own DXIL, with shader model 6.6 handles last */
struct TcOpsBuild
{
    PCSTR FxcProfile;
    PCWSTR DxcProfile;
    PCWSTR DxcExtra;
    PCWSTR DxcDefine;
};

static const TcOpsBuild TcOpsBuilds[] =
{
    { "cs_5_1", NULL, NULL },
    { NULL, L"cs_6_0", NULL },
    { NULL, L"cs_6_6", NULL },
};

static
HRESULT
TcOpsCompile(
    _In_z_ PCSTR Source,
    _In_z_ PCSTR Entry,
    _In_ const TcOpsBuild &Build,
    _Out_ ID3DBlob **Blob)
{
    ID3DBlob *Errors = NULL;
    HRESULT hr;

    if (Build.FxcProfile != NULL)
    {
        hr = D3DCompile(Source, strlen(Source), "ops", NULL, NULL, Entry, Build.FxcProfile, 0, 0, Blob, &Errors);
        if (Errors)
        {
            if (FAILED(hr))
                printf("%s", (const char *)Errors->GetBufferPointer());
            Errors->Release();
        }
        return hr;
    }
    std::wstring Wide(Entry, Entry + strlen(Entry));

    return TcDxcCompile(Source, Wide.c_str(), Build.DxcProfile, Build.DxcExtra, Build.DxcDefine, Blob);
}

/*
 * The resources every compute operation scene binds: a raw output (u0), a counted structured
 * buffer (u1), a typed grid (u2) and a typed buffer (u3) to write; an eight bit image with two
 * mips (t0), a float image (t1), an image array (t2) and raw bytes (t3) to read, two more in
 * space 1, and point, linear, comparison and mirrored samplers.
 */
struct TcOpsKit
{
    static constexpr UINT OutputSize = 16384;
    static constexpr UINT TableSize = 12;

    HRESULT
    Create(
        _In_ ID3D12Device *Device,
        _In_ TcQueue *Queue,
        _Inout_ TcDrawKit *Kit)
    {
        D3D12_DESCRIPTOR_RANGE Ranges[3] =
        {
            { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },
            { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },
            { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 1, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND },
        };
        D3D12_DESCRIPTOR_RANGE SamplerRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 4, 0, 0, 0 };
        D3D12_ROOT_PARAMETER Parameters[3] = {};
        D3D12_ROOT_SIGNATURE_DESC Desc = {};
        HRESULT hr;

        Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        Parameters[0].Constants.Num32BitValues = 4;
        Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters[1].DescriptorTable.NumDescriptorRanges = ARRAYSIZE(Ranges);
        Parameters[1].DescriptorTable.pDescriptorRanges = Ranges;
        Parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters[2].DescriptorTable.NumDescriptorRanges = 1;
        Parameters[2].DescriptorTable.pDescriptorRanges = &SamplerRange;
        Desc.NumParameters = ARRAYSIZE(Parameters);
        Desc.pParameters = Parameters;
        Desc.Flags = Flags;
        hr = TcRootSignature(Device, &Desc, &Signature);
        Kit->Keep(Signature);
        if (FAILED(hr))
            return hr;

        Output = Kit->Keep(TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, OutputSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
        Counted = Kit->Keep(TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
        Counter = Kit->Keep(TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
        Grid = Kit->Keep(TcTexture(Device, DXGI_FORMAT_R32_UINT, 8, 8, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COPY_DEST));
        Typed = Kit->Keep(TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
        Image = Kit->Keep(TcTexture(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, 1, 2, D3D12_RESOURCE_FLAG_NONE,
                                    D3D12_RESOURCE_STATE_COPY_DEST));
        Floats = Kit->Keep(TcTexture(Device, DXGI_FORMAT_R32_FLOAT, 8, 8, 1, 1, D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_COPY_DEST));
        Layers = Kit->Keep(TcTexture(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 3, 1, D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_COPY_DEST));
        Raw = Kit->Keep(TcBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_FLAG_NONE));
        Staging = Kit->Keep(TcBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, 4 * 65536, D3D12_RESOURCE_FLAG_NONE));
        Readback = Kit->Keep(TcBuffer(Device, D3D12_HEAP_TYPE_READBACK, OutputSize, D3D12_RESOURCE_FLAG_NONE));
        Views = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, TableSize, TRUE, Kit);
        Samplers = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 4, TRUE, Kit);
        if (Output == NULL || Counted == NULL || Counter == NULL || Grid == NULL || Typed == NULL || Image == NULL || Floats == NULL ||
            Layers == NULL || Raw == NULL || Staging == NULL || Readback == NULL || Views == NULL || Samplers == NULL)
        {
            return E_OUTOFMEMORY;
        }
        hr = TcFill(Raw, 256, 3);
        if (SUCCEEDED(hr))
            hr = Fill(Queue);
        if (SUCCEEDED(hr))
            Describe(Device);
        return hr;
    }

    /* Upload pattern: images, then the float image's values, then zeroes and seeds for the outputs */
    HRESULT
    Fill(
        _In_ TcQueue *Queue)
    {
        BYTE *Data = NULL;
        HRESULT hr = Staging->Map(0, NULL, reinterpret_cast<VOID **>(&Data));

        if (FAILED(hr))
            return hr;
        for (UINT Index = 0; Index < 65536; Index++)
            Data[Index] = (BYTE)(Index * 13 + 7 + (Index >> 8) * 5);
        for (UINT Index = 0; Index < 64; Index++)
        {
            FLOAT Value = (FLOAT)((INT)(Index * 37 % 64) - 20) * 0.375f;

            memcpy(Data + 65536 + (Index / 8) * 256 + (Index % 8) * 4, &Value, sizeof(Value));
        }
        memset(Data + 2 * 65536, 0, 65536);
        for (UINT Index = 0; Index < 64; Index++)
        {
            UINT Seed = Index * 0x01010101u ^ 0x5A5A0000u;

            memcpy(Data + 3 * 65536 + Index * 4, &Seed, sizeof(Seed));
        }
        Staging->Unmap(0, NULL);

        CopyImage(Queue, Image, 0, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        CopyImage(Queue, Image, 1, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 4096);
        CopyImage(Queue, Floats, 0, 8, 8, DXGI_FORMAT_R32_FLOAT, 65536);
        for (UINT Slice = 0; Slice < 3; Slice++)
            CopyImage(Queue, Layers, Slice, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 8192 + Slice * 1024);
        TcBarrier(Queue->List, Image, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        TcBarrier(Queue->List, Floats, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        TcBarrier(Queue->List, Layers, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return Queue->Submit();
    }

    VOID
    CopyImage(
        _In_ TcQueue *Queue,
        _In_ ID3D12Resource *Target,
        _In_ UINT Subresource,
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ DXGI_FORMAT Format,
        _In_ UINT64 Offset)
    {
        D3D12_TEXTURE_COPY_LOCATION To = {};
        D3D12_TEXTURE_COPY_LOCATION From = {};

        To.pResource = Target;
        To.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        To.SubresourceIndex = Subresource;
        From.pResource = Staging;
        From.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        From.PlacedFootprint.Offset = Offset;
        From.PlacedFootprint.Footprint.Format = Format;
        From.PlacedFootprint.Footprint.Width = Width;
        From.PlacedFootprint.Footprint.Height = Height;
        From.PlacedFootprint.Footprint.Depth = 1;
        From.PlacedFootprint.Footprint.RowPitch = 256;
        Queue->List->CopyTextureRegion(&To, 0, 0, 0, &From, NULL);
    }

    /* Table: t0 to t3, u0 to u3, then t0 and t1 of space 1 */
    VOID
    Describe(
        _In_ ID3D12Device *Device)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC Bytes = {};
        D3D12_SHADER_RESOURCE_VIEW_DESC Slices = {};
        D3D12_UNORDERED_ACCESS_VIEW_DESC Access = {};
        D3D12_CONSTANT_BUFFER_VIEW_DESC Constants = {};
        D3D12_SAMPLER_DESC Sampler = {};

        Device->CreateShaderResourceView(Image, NULL, TcHandle(Device, Views, 0));
        Device->CreateShaderResourceView(Floats, NULL, TcHandle(Device, Views, 1));
        Device->CreateShaderResourceView(Layers, NULL, TcHandle(Device, Views, 2));
        Bytes.Format = DXGI_FORMAT_R32_TYPELESS;
        Bytes.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        Bytes.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        Bytes.Buffer.NumElements = 64;
        Bytes.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        Device->CreateShaderResourceView(Raw, &Bytes, TcHandle(Device, Views, 3));

        Access.Format = DXGI_FORMAT_R32_TYPELESS;
        Access.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        Access.Buffer.NumElements = OutputSize / 4;
        Access.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        Device->CreateUnorderedAccessView(Output, NULL, &Access, TcHandle(Device, Views, 4));
        Access.Format = DXGI_FORMAT_UNKNOWN;
        Access.Buffer.NumElements = 64;
        Access.Buffer.StructureByteStride = 4;
        Access.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        Device->CreateUnorderedAccessView(Counted, Counter, &Access, TcHandle(Device, Views, 5));
        Device->CreateUnorderedAccessView(Grid, NULL, NULL, TcHandle(Device, Views, 6));
        Access.Format = DXGI_FORMAT_R32_UINT;
        Access.Buffer.StructureByteStride = 0;
        Device->CreateUnorderedAccessView(Typed, NULL, &Access, TcHandle(Device, Views, 7));

        Device->CreateShaderResourceView(Floats, NULL, TcHandle(Device, Views, 8));
        Slices.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Slices.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        Slices.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(2, 1, 0, 3);
        Slices.Texture2D.MostDetailedMip = 1;
        Slices.Texture2D.MipLevels = 1;
        Device->CreateShaderResourceView(Image, &Slices, TcHandle(Device, Views, 9));
        Constants.BufferLocation = Raw->GetGPUVirtualAddress();
        Constants.SizeInBytes = 256;
        Device->CreateConstantBufferView(&Constants, TcHandle(Device, Views, 10));

        Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        Sampler.MaxLOD = D3D12_FLOAT32_MAX;
        Device->CreateSampler(&Sampler, TcHandle(Device, Samplers, 0));
        Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        Device->CreateSampler(&Sampler, TcHandle(Device, Samplers, 1));
        Sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
        Sampler.BorderColor[0] = 0.5f;
        Device->CreateSampler(&Sampler, TcHandle(Device, Samplers, 2));
        Sampler.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
        Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        Sampler.MipLODBias = 0.5f;
        Device->CreateSampler(&Sampler, TcHandle(Device, Samplers, 3));
    }

    /* Output to zero, the counted buffer, typed buffer and grid to their seeds, the counter to five */
    VOID
    Reset(
        _In_ TcQueue *Queue)
    {
        static const UINT Five = 5;
        BYTE *Data = NULL;

        if (SUCCEEDED(Staging->Map(0, NULL, reinterpret_cast<VOID **>(&Data))))
        {
            memcpy(Data + 2 * 65536 + 32768, &Five, sizeof(Five));
            Staging->Unmap(0, NULL);
        }
        Queue->List->CopyBufferRegion(Output, 0, Staging, 2 * 65536, OutputSize);
        Queue->List->CopyBufferRegion(Counted, 0, Staging, 3 * 65536, 256);
        Queue->List->CopyBufferRegion(Typed, 0, Staging, 3 * 65536 + 4, 252);
        Queue->List->CopyBufferRegion(Counter, 0, Staging, 2 * 65536 + 32768, 4);
        CopyImage(Queue, Grid, 0, 8, 8, DXGI_FORMAT_R32_UINT, 3 * 65536);
        for (ID3D12Resource *Buffer : { Output, Counted, Typed, Counter, Grid })
            TcBarrier(Queue->List, Buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    VOID
    Bind(
        _In_ ID3D12GraphicsCommandList *List,
        _In_ ID3D12Device *Device,
        _In_reads_(4) const UINT *Constants)
    {
        ID3D12DescriptorHeap *Heaps[2] = { Views, Samplers };

        List->SetDescriptorHeaps(2, Heaps);
        List->SetComputeRootSignature(Signature);
        List->SetComputeRoot32BitConstants(0, 4, Constants, 0);
        List->SetComputeRootDescriptorTable(1, TcGpuHandle(Device, Views, 0));
        List->SetComputeRootDescriptorTable(2, TcGpuHandle(Device, Samplers, 0));
    }

    /* Reads back every output and moves them to copy dest again for the next build */
    HRESULT
    Collect(
        _In_ ID3D12Device *Device,
        _In_ TcQueue *Queue,
        _Inout_ TcResult *Result)
    {
        struct { ID3D12Resource *Buffer; UINT Size; } Parts[] = { { Output, OutputSize }, { Counted, 256 }, { Counter, 4 }, { Typed, 256 } };
        HRESULT hr = S_OK;

        for (const auto &Part : Parts)
        {
            TcBarrier(Queue->List, Part.Buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Queue->List->CopyBufferRegion(Readback, 0, Part.Buffer, 0, Part.Size);
            hr = Queue->Submit();
            TcAppend(Result, &hr, sizeof(hr));
            if (FAILED(hr))
                return hr;
            TcReadback(Readback, Part.Size, Result);
            TcBarrier(Queue->List, Part.Buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        }
        TcBarrier(Queue->List, Grid, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        hr = TcReadTexture(Device, Queue, Grid, 1, Result);
        TcBarrier(Queue->List, Grid, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        return hr;
    }

    ID3D12RootSignature *Signature = NULL;
    ID3D12Resource *Output = NULL;
    ID3D12Resource *Counted = NULL;
    ID3D12Resource *Counter = NULL;
    ID3D12Resource *Grid = NULL;
    ID3D12Resource *Typed = NULL;
    ID3D12Resource *Image = NULL;
    ID3D12Resource *Floats = NULL;
    ID3D12Resource *Layers = NULL;
    ID3D12Resource *Raw = NULL;
    ID3D12Resource *Staging = NULL;
    ID3D12Resource *Readback = NULL;
    ID3D12DescriptorHeap *Views = NULL;
    ID3D12DescriptorHeap *Samplers = NULL;
    D3D12_ROOT_SIGNATURE_FLAGS Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
};

static const char TcOpsCommon[] =
    "#ifdef SHORT_KNOBS\n"
    "cbuffer Knobs : register(b0) { uint16_t2 KnobShorts; float16_t2 KnobHalves; int16_t4 KnobSigned; };\n"
    "#else\n"
    "cbuffer Knobs : register(b0) { uint4 Knob; };\n"
    "#endif\n"
    "RWByteAddressBuffer Out : register(u0);\n"
    "RWStructuredBuffer<uint> Counted : register(u1);\n"
    "RWTexture2D<uint> Grid : register(u2);\n"
    "RWBuffer<uint> Typed : register(u3);\n"
    "Texture2D Image : register(t0);\n"
    "Texture2D<float> Floats : register(t1);\n"
    "Texture2DArray Layers : register(t2);\n"
    "ByteAddressBuffer Raw : register(t3);\n"
    "Texture2D SpacePair[2] : register(t0, space1);\n"
    "SamplerState Point : register(s0);\n"
    "SamplerState Smooth : register(s1);\n"
    "SamplerComparisonState Compare : register(s2);\n"
    "SamplerState Mirror : register(s3);\n";

/* Runs one compute program, built each way the scene lists, over the operation kit */
static
HRESULT
TcOpsRun(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result,
    _In_z_ PCSTR Source,
    _In_z_ PCSTR Entry,
    _In_reads_(BuildCount) const TcOpsBuild *Builds,
    _In_ UINT BuildCount,
    _In_ UINT Groups,
    _In_ D3D12_ROOT_SIGNATURE_FLAGS Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE)
{
    static const UINT Constants[4] = { 3, 0x9E3779B9u, 5, 0x40490FDBu };
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcOpsKit Ops;
    TcQueue Queue;
    HRESULT hr;

    Ops.Flags = Flags;
    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = Ops.Create(Device, &Queue, &Kit);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;

    for (UINT Index = 0; Index < BuildCount; Index++)
    {
        ID3DBlob *Shader = NULL;
        ID3D12PipelineState *State = NULL;

        hr = TcOpsCompile((std::string(TcOpsCommon) + Source).c_str(), Entry, Builds[Index], &Shader);
        Kit.Keep(Shader);
        if (SUCCEEDED(hr))
        {
            D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {};

            Desc.pRootSignature = Ops.Signature;
            Desc.CS = TcCode(Shader);
            hr = Device->CreateComputePipelineState(&Desc, IID_PPV_ARGS(&State));
            Kit.Keep(State);
        }
        TcAppend(Result, &hr, sizeof(hr));
        if (FAILED(hr))
            continue;
        Ops.Reset(&Queue);
        Ops.Bind(Queue.List, Device, Constants);
        Queue.List->SetPipelineState(State);
        Queue.List->Dispatch(Groups, 1, 1);
        hr = Ops.Collect(Device, &Queue, Result);
        if (FAILED(hr))
            return hr;
    }
    TcLogSince(LogStart, Result);
    return hr;
}


/* Switches with shared and default cases, and arrays indexed by values the shader computes */
static const char TcOpsFlowSource[] =
    "groupshared uint Seen[64];\n"
    "[numthreads(64, 1, 1)] void flow(uint t : SV_DispatchThreadID, uint g : SV_GroupIndex)\n"
    "{\n"
    "    uint v = 0;\n"
    "    switch ((t + Knob.x) % 9)\n"
    "    {\n"
    "        case 0: v = 11; break;\n"
    "        case 1: v = t * 3; break;\n"
    "        case 2: case 3: v = t ^ 0x55; break;\n"
    "        case 5: v = Knob.y >> (t & 7); break;\n"
    "        case 8: v = 0xFFFFFFFF - t; break;\n"
    "        default: v = t + 1000; break;\n"
    "    }\n"
    "    uint Table[12];\n"
    "    float4 Vectors[5];\n"
    "    [loop] for (uint i = 0; i < 12; i++)\n"
    "        Table[i] = i * i + t;\n"
    "    [loop] for (uint j = 0; j < 5; j++)\n"
    "        Vectors[j] = float4(j, t, j * t, 0.5 * j);\n"
    "    Table[(t * 7) % 12] += v;\n"
    "    Vectors[t % 5].y += 2.5;\n"
    "    uint Sum = 0;\n"
    "    [loop] for (uint k = 0; k < 12; k += 3)\n"
    "    {\n"
    "        if (Table[k] > 40 + t)\n"
    "            break;\n"
    "        Sum += Table[k];\n"
    "    }\n"
    "    Seen[g] = Sum;\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    Out.Store(t * 16, v);\n"
    "    Out.Store(t * 16 + 4, Table[(t * 5 + Knob.z) % 12]);\n"
    "    Out.Store(t * 16 + 8, asuint(Vectors[(t + 2) % 5].y + Vectors[t % 5].w));\n"
    "    Out.Store(t * 16 + 12, Seen[63 - g] + Sum);\n"
    "}\n";

static
HRESULT
SceneOpsFlow(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    return TcOpsRun(Device, Result, TcOpsFlowSource, "flow", TcOpsBuilds, ARRAYSIZE(TcOpsBuilds), 2);
}

/* Loads, sizes, level and gradient samples, comparisons and gathers, through fixed and computed resource indices */
static const char TcOpsReadSource[] =
    "[numthreads(64, 1, 1)] void reads(uint t : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Base = t * 256;\n"
    "    float2 Uv = float2((t % 8) * 0.137 - 0.2, (t / 8) * 0.161 - 0.3);\n"
    "    uint w, h, m, e;\n"
    "    Image.GetDimensions(t & 1, w, h, m);\n"
    "    Layers.GetDimensions(w, h, e);\n"
    "    uint4 Sizes = uint4(w, h, m, e);\n"
    "    Floats.GetDimensions(w, h);\n"
    "    Raw.GetDimensions(e);\n"
    "    Sizes += uint4(w * 100, h * 100, e * 1000, 0);\n"
    "    Out.Store4(Base, Sizes);\n"
    "    Out.Store4(Base + 16, asuint(Image.Load(int3(t % 8, t / 8, 0))));\n"
    "    Out.Store4(Base + 32, asuint(Image.SampleLevel(Smooth, Uv, (t % 5) * 0.3)));\n"
    "    Out.Store4(Base + 48, asuint(Image.SampleGrad(Smooth, Uv, float2(0.05 * (t % 3), 0.01), float2(0.02, 0.3 * (t % 4)))));\n"
    "    Out.Store4(Base + 64, asuint(Image.SampleLevel(Mirror, Uv * 2.5, 0.4, int2(1, -2))));\n"
    "    Out.Store(Base + 80, asuint(Floats.SampleCmpLevelZero(Compare, Uv, (t % 7) * 1.5 - 4.0)));\n"
    "    Out.Store(Base + 84, asuint(Floats.SampleLevel(Smooth, Uv, 0)));\n"
    "    Out.Store(Base + 88, asuint(Floats.SampleLevel(Point, Uv, 0, int2(-1, 1))));\n"
    "    Out.Store(Base + 92, asuint(Layers.SampleLevel(Smooth, float3(Uv, t % 4), 0).y));\n"
    "    Out.Store4(Base + 96, asuint(Image.GatherRed(Point, Uv)));\n"
    "    Out.Store4(Base + 112, asuint(Image.GatherAlpha(Smooth, Uv, int2(1, 0))));\n"
    "    Out.Store4(Base + 128, asuint(Floats.Gather(Point, Uv * 1.3)));\n"
    "    Out.Store4(Base + 144, asuint(Floats.GatherCmp(Compare, Uv, 1.0)));\n"
    "    Out.Store4(Base + 160, asuint(SpacePair[t & 1].SampleLevel(Smooth, Uv, 0)));\n"
    "    Out.Store4(Base + 176, asuint(SpacePair[(t >> 1) & 1].Load(int3(t % 4, t / 16, 0))));\n"
    "    Out.Store4(Base + 192, Raw.Load4((t * 4) % 240));\n"
    "    Out.Store2(Base + 208, Raw.Load2((t * 12) % 248));\n"
    "    Out.Store4(Base + 216, asuint(Layers.Load(int4(t % 4, (t / 4) % 4, t % 3, 0))));\n"
    "    Out.Store2(Base + 232, asuint(Image.Load(int3(t % 4, t % 3, 1), int2(1, 0)).xw));\n"
    "    Out.Store2(Base + 240, asuint(Image.SampleLevel(Point, Uv, 1).yz));\n"
    "    Out.Store2(Base + 248, asuint(SpacePair[1].Gather(Point, Uv).xy));\n"
    "}\n";

static
HRESULT
SceneOpsRead(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    return TcOpsRun(Device, Result, TcOpsReadSource, "reads", TcOpsBuilds, ARRAYSIZE(TcOpsBuilds), 1);
}

/*
 * Atomics on group shared memory, raw, structured, typed buffers and a typed texture, and the
 * structured buffer's counter. Returned values are only kept where they cannot depend on the
 * order threads run in.
 */
static const char TcOpsAtomSource[] =
    "groupshared uint Pool[16];\n"
    "[numthreads(64, 1, 1)] void atoms(uint t : SV_DispatchThreadID, uint g : SV_GroupIndex)\n"
    "{\n"
    "    uint Old;\n"
    "    if (g < 16)\n"
    "        Pool[g] = g * 3;\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    InterlockedAdd(Pool[t % 16], t);\n"
    "    InterlockedMax(Pool[(t * 5) % 16], t * 7);\n"
    "    InterlockedXor(Pool[(t * 3) % 16], 1u << (t % 13));\n"
    "    InterlockedMin(Pool[15], 100 - t);\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    if (g < 16)\n"
    "        Out.Store(g * 4, Pool[g]);\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    if (g < 8)\n"
    "        Pool[g] = 1000 + g;\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    if (g < 8)\n"
    "    {\n"
    "        InterlockedExchange(Pool[g], g * 11, Old);\n"
    "        Out.Store(64 + g * 4, Old);\n"
    "        InterlockedCompareExchange(Pool[g], (g & 1) ? g * 11 : 5, 0xABC0 + g, Old);\n"
    "        Out.Store(96 + g * 4, Old);\n"
    "        InterlockedCompareStore(Pool[(g + 1) % 8], 0xABC0 + ((g + 1) % 8), g);\n"
    "    }\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    if (g < 8)\n"
    "        Out.Store(128 + g * 4, Pool[g]);\n"
    "    Out.InterlockedAdd(256 + (t % 8) * 4, t + 1);\n"
    "    Out.InterlockedOr(288 + (t % 4) * 4, 1u << (t % 29));\n"
    "    Out.InterlockedAnd(304, ~(1u << (t % 31)));\n"
    "    Out.InterlockedMax(320 + (t % 2) * 4, t * 13 % 41);\n"
    "    Out.InterlockedMin(328, 1000 - t * 3);\n"
    "    InterlockedMin(Typed[t % 16], 0x5A5A0000u + t * 977);\n"
    "    InterlockedMax(Typed[16 + t % 8], t * 131);\n"
    "    InterlockedAdd(Typed[24], 3);\n"
    "    Out.InterlockedExchange(1024 + t * 4, t * 3, Old);\n"
    "    Out.Store(2048 + t * 4, Old);\n"
    "    Out.InterlockedCompareExchange(1024 + t * 4, (t & 1) ? t * 3 : 9, 77 + t, Old);\n"
    "    Out.Store(2304 + t * 4, Old);\n"
    "    Out.InterlockedCompareStore(2560 + t * 4, 0, t ^ 0x33);\n"
    "    InterlockedOr(Grid[uint2(t % 8, t / 8)], 1u << (t % 32), Old);\n"
    "    Out.Store(3072 + t * 4, Old);\n"
    "    InterlockedAdd(Counted[t], t * 5, Old);\n"
    "    Out.Store(3328 + t * 4, Old);\n"
    "    if ((t & 1) == 0)\n"
    "        Out.InterlockedAdd(4096 + (Counted.IncrementCounter() % 128) * 4, 1);\n"
    "    AllMemoryBarrierWithGroupSync();\n"
    "    if (t % 3 == 0)\n"
    "        Out.InterlockedAdd(4608 + (Counted.IncrementCounter() % 128) * 4, 1);\n"
    "}\n";

static
HRESULT
SceneOpsAtom(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    return TcOpsRun(Device, Result, TcOpsAtomSource, "atoms", TcOpsBuilds, ARRAYSIZE(TcOpsBuilds), 1);
}

/* Samples far outside the unit square and below zero, where power of two sizes wrap texel indices instead of coordinates */
static const char TcOpsAddressSource[] =
    "[numthreads(64, 1, 1)] void address(uint t : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Base = t * 256;\n"
    "    float2 Uv = float2((t % 8) * 3.731 - 17.2, (t / 8) * -5.117 + 9.3);\n"
    "    float2 Far = Uv * 4099.37 + float2(0.3, -0.7);\n"
    "    float2 Near = float2(-0.0013 * t, 0.00007 * t - 0.002);\n"
    "    Out.Store4(Base, asuint(Image.SampleLevel(Smooth, Uv, 0)));\n"
    "    Out.Store4(Base + 16, asuint(Image.SampleLevel(Smooth, Far, 0)));\n"
    "    Out.Store4(Base + 32, asuint(Image.SampleLevel(Mirror, Uv, 0, int2(-3, 2))));\n"
    "    Out.Store4(Base + 48, asuint(Image.SampleLevel(Mirror, Far, 0)));\n"
    "    Out.Store4(Base + 64, asuint(Image.SampleLevel(Smooth, Near, 1)));\n"
    "    Out.Store4(Base + 80, asuint(Image.SampleLevel(Smooth, Near, 0, int2(-2, 3))));\n"
    "    Out.Store4(Base + 96, asuint(Image.SampleLevel(Point, Uv * 0.07, 0, int2(2, -1))));\n"
    "    Out.Store(Base + 112, asuint(Floats.SampleLevel(Smooth, Uv, 0)));\n"
    "    Out.Store(Base + 116, asuint(Floats.SampleLevel(Smooth, Far, 0)));\n"
    "    Out.Store(Base + 120, asuint(Floats.SampleLevel(Mirror, -Uv, 0)));\n"
    "    Out.Store(Base + 124, asuint(Floats.SampleCmpLevelZero(Compare, Uv * 0.2, 0.5)));\n"
    "    Out.Store4(Base + 128, asuint(Image.GatherGreen(Smooth, Uv)));\n"
    "    Out.Store4(Base + 144, asuint(Image.GatherBlue(Smooth, Far, int2(-1, 1))));\n"
    "    Out.Store4(Base + 160, asuint(Layers.SampleLevel(Smooth, float3(Far, t % 3), 0)));\n"
    "    Out.Store4(Base + 176, asuint(Floats.Gather(Smooth, Near * 7.0)));\n"
    "    Out.Store4(Base + 192, asuint(Image.SampleLevel(Smooth, Uv * 1e6, 0)));\n"
    "}\n";

static
HRESULT
SceneOpsAddress(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    return TcOpsRun(Device, Result, TcOpsAddressSource, "address", TcOpsBuilds, ARRAYSIZE(TcOpsBuilds), 1);
}

/* Gradient samples under a lowest level of detail, and the residency status every sample form returns */
static const char TcOpsClampSource[] =
    "[numthreads(64, 1, 1)] void clamp(uint t : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Base = t * 256;\n"
    "    float2 Uv = float2((t % 8) * 0.137 - 0.2, (t / 8) * 0.161 - 0.3);\n"
    "    float2 Across = float2(0.03 * (t % 5), 0.02);\n"
    "    float2 Down = float2(0.01, 0.07 * (t % 3));\n"
    "    float Lowest = (t % 6) * 0.3 - 0.4;\n"
    "    uint Status[6];\n"
    "    Out.Store4(Base, asuint(Image.SampleGrad(Smooth, Uv, Across, Down, int2(0, 0), Lowest)));\n"
    "    Out.Store4(Base + 16, asuint(Image.SampleGrad(Mirror, Uv * 1.7, Across * 2.0, Down, int2(1, -1), Lowest + 0.5, Status[0])));\n"
    "    Out.Store4(Base + 32, asuint(Image.SampleLevel(Smooth, Uv, (t % 4) * 0.4, int2(0, 0), Status[1])));\n"
    "    Out.Store(Base + 48, asuint(Floats.SampleCmpLevelZero(Compare, Uv, (t % 7) * 1.5 - 4.0, int2(0, 0), Status[2])));\n"
    "    Out.Store(Base + 52, asuint(Floats.SampleGrad(Smooth, Uv, Across, Down, int2(0, 0), 2.0)));\n"
    "    Out.Store4(Base + 64, asuint(Layers.SampleGrad(Smooth, float3(Uv, t % 3), Across * 9.0, Down * 9.0, int2(0, 0), Lowest * 2.0, Status[3])));\n"
    "    Out.Store4(Base + 80, uint4(Status[0], Status[1], Status[2], Status[3]));\n"
    "    Out.Store4(Base + 96, uint4(CheckAccessFullyMapped(Status[0]), CheckAccessFullyMapped(Status[1]),\n"
    "                                CheckAccessFullyMapped(Status[2]), CheckAccessFullyMapped(Status[3])));\n"
    "}\n";

static
HRESULT
SceneOpsClamp(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    return TcOpsRun(Device, Result, TcOpsClampSource, "clamp", TcOpsBuilds, ARRAYSIZE(TcOpsBuilds), 1);
}

/* Sizes of every unordered access view kind, next to the read only ones */
static const char TcOpsSizeSource[] =
    "[numthreads(64, 1, 1)] void sizes(uint t : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Base = t * 64;\n"
    "    uint w, h, n, s;\n"
    "    Grid.GetDimensions(w, h);\n"
    "    Typed.GetDimensions(n);\n"
    "    Out.Store4(Base, uint4(w, h, n, t));\n"
    "    Counted.GetDimensions(n, s);\n"
    "    Out.GetDimensions(w);\n"
    "    Out.Store4(Base + 16, uint4(n, s, w, Grid[uint2(t % 8, t / 8)]));\n"
    "    Image.GetDimensions(t % 3, w, h, n);\n"
    "    Out.Store4(Base + 32, uint4(w, h, n, Typed[t % 64]));\n"
    "}\n";

static
HRESULT
SceneOpsSize(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    return TcOpsRun(Device, Result, TcOpsSizeSource, "sizes", TcOpsBuilds, ARRAYSIZE(TcOpsBuilds), 1);
}

/* Wave size, lane indices and every wave reduction, prefix, read and quad operation, uniform and divergent */
static const char TcOpsWaveSource[] =
    "[numthreads(64, 1, 1)] void waves(uint t : SV_DispatchThreadID, uint g : SV_GroupIndex)\n"
    "{\n"
    "    uint Base = t * 160;\n"
    "    uint Lane = WaveGetLaneIndex();\n"
    "    uint4 Ballot = WaveActiveBallot((t * 7) % 3 == 0);\n"
    "    float Wide = ((t & 3) == 0) ? 16777216.0 : ((t & 3) == 3) ? -16777216.0 : 1.0 + (t & 7) * 0.25;\n"
    "    float Small = (t % 5) * 0.1 + 0.3;\n"
    "    int Signed = (int)(t * 2654435761u) >> 20;\n"
    "    Out.Store4(Base, uint4(WaveGetLaneCount(), Lane, WaveIsFirstLane(), WaveActiveCountBits(t & 1)));\n"
    "    Out.Store4(Base + 16, Ballot);\n"
    "    Out.Store4(Base + 32, uint4(WaveActiveSum(t), WavePrefixSum(t + 1), WaveReadLaneFirst(t * 3), WaveReadLaneAt(t, 1)));\n"
    "    Out.Store4(Base + 48, uint4(QuadReadAcrossX(t), QuadReadAcrossY(t), QuadReadAcrossDiagonal(t), QuadReadLaneAt(t, 2)));\n"
    "    Out.Store4(Base + 64, asuint(float4(WaveActiveSum(Wide), WavePrefixSum(Wide), WaveActiveProduct(Small), WavePrefixProduct(Small))));\n"
    "    Out.Store4(Base + 80, uint4(asuint(WaveActiveMin(Small - 0.5)), asuint(WaveActiveMax(-Small)), WaveActiveMin(Signed), WaveActiveMax(Signed)));\n"
    "    Out.Store4(Base + 96, uint4(WaveActiveMin((uint)Signed), WaveActiveMax((uint)Signed), WaveActiveBitAnd(t | 0x30), WaveActiveBitOr(t << 3)));\n"
    "    Out.Store4(Base + 112, uint4(WaveActiveBitXor(t * 5), WaveActiveAllTrue(t % 4 != 3) | (WaveActiveAnyTrue(t % 8 == 5) << 1) |\n"
    "                                 (WaveActiveAllEqual(t / 4) << 2) | (WaveActiveAllEqual(Small) << 3), WavePrefixCountBits(t % 3 == 1),\n"
    "                                 WavePrefixProduct(t + 1)));\n"
    "    if (t % 3 != 0)\n"
    "    {\n"
    "        uint4 Some = WaveActiveBallot(true);\n"
    "        Out.Store4(Base + 128, uint4(WaveActiveSum(t), WavePrefixSum(t), WaveIsFirstLane() | (WaveReadLaneFirst(t) << 4), Some.x));\n"
    "        Out.Store(Base + 144, WaveActiveCountBits(true) | (WaveActiveMin(t) << 8) | (asuint(WaveActiveSum(Small)) & 0xFFFF0000));\n"
    "    }\n"
    "}\n";

/* Groups that do not fill their last wave, laid out in two dimensions */
static const char TcOpsWavePartSource[] =
    "[numthreads(5, 3, 1)] void parts(uint3 t : SV_DispatchThreadID, uint g : SV_GroupIndex, uint3 Group : SV_GroupID)\n"
    "{\n"
    "    uint Flat = Group.x * 15 + g;\n"
    "    uint Base = Flat * 32;\n"
    "    uint4 Ballot = WaveActiveBallot(true);\n"
    "    Out.Store4(Base, uint4(WaveGetLaneIndex(), WaveActiveSum(Flat), WavePrefixSum(g), Ballot.x));\n"
    "    Out.Store4(Base + 16, uint4(QuadReadAcrossX(Flat), QuadReadAcrossDiagonal(t.y), WaveReadLaneFirst(t.x), WaveActiveCountBits(t.y == 1)));\n"
    "}\n";

/*
 * Lane reads past the wave and from lanes that do not run, matches and partitioned prefixes,
 * and wave operations on 16 bit values
 */
static const char TcOpsWaveMoreSource[] =
    "[numthreads(64, 1, 1)] void more(uint t : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Base = t * 128;\n"
    "    float f = t * 1.37 - 20.0;\n"
    "    uint Key = (t * 7) % 3;\n"
    "    Out.Store4(Base, uint4(WaveReadLaneAt(t, t % 61), WaveReadLaneAt(t * 3, 7 - (t & 7)), WaveReadLaneAt(t, (t * 13) % 64), QuadReadLaneAt(t * 9, (t * 11) % 5)));\n"
    "    Out.Store4(Base + 16, WaveMatch(Key));\n"
    "    Out.Store4(Base + 32, WaveMatch(f > 0.0 ? 1.0 : -0.0));\n"
    "    uint4 Group = WaveMatch(t & 2);\n"
    "    Out.Store4(Base + 48, uint4(WaveMultiPrefixSum(t + 1, Group), WaveMultiPrefixProduct(t + 2, Group),\n"
    "                                WaveMultiPrefixCountBits(t % 3 != 1, Group), WaveMultiPrefixBitOr(1u << t % 32, Group)));\n"
    "    Out.Store4(Base + 64, uint4(asuint(WaveMultiPrefixSum(f, Group)), WaveMultiPrefixBitAnd(t | 0xF0, Group), WaveMultiPrefixBitXor(t * 5, Group),\n"
    "                                asuint(WaveMultiPrefixProduct(f, WaveMatch(Key)))));\n"
    "    if (t % 4 != 1)\n"
    "    {\n"
    "        Out.Store4(Base + 80, uint4(WaveReadLaneAt(t + 100, 1), QuadReadAcrossX(t + 200), QuadReadLaneAt(t + 300, 1), WaveReadLaneAt(f, (t + 1) % 61)));\n"
    "        Out.Store4(Base + 96, uint4(QuadReadAcrossDiagonal(t + 400), WaveMatch(t / 2).x, WaveMultiPrefixSum(t, uint4(0xF, 0, 0, 0)), QuadReadAcrossY(f)));\n"
    "    }\n"
    "    float16_t h = (float16_t)(t * 0.731 - 9.0);\n"
    "    int16_t s = (int16_t)(t * 3001 - 30000);\n"
    "    uint16_t u = (uint16_t)(t * 5003 + 7);\n"
    "    Out.Store4(Base + 112, uint4(asuint((float)WaveActiveSum(h)) ^ asuint((float)WavePrefixProduct(h)), (uint)(int)WaveActiveMin(s) + ((uint)(int)WaveActiveMax(s) << 16),\n"
    "                                 (uint)WaveActiveSum(u) | ((uint)WavePrefixSum(s) << 16), (uint)WaveReadLaneAt(u, 2) | ((uint)QuadReadAcrossX(s) << 16) ^ (uint)WaveActiveMax(u)));\n"
    "}\n";

static const TcOpsBuild TcOpsWaveBuilds[] =
{
    { NULL, L"cs_6_0", NULL },
    { NULL, L"cs_6_6", NULL },
};

static const TcOpsBuild TcOpsWaveMoreBuilds[] =
{
    { NULL, L"cs_6_5", L"-enable-16bit-types" },
};

static
HRESULT
SceneOpsWave(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    HRESULT hr = TcOpsRun(Device, Result, TcOpsWaveSource, "waves", TcOpsWaveBuilds, ARRAYSIZE(TcOpsWaveBuilds), 1);

    if (SUCCEEDED(hr))
        hr = TcOpsRun(Device, Result, TcOpsWavePartSource, "parts", TcOpsWaveBuilds, ARRAYSIZE(TcOpsWaveBuilds), 3);
    if (SUCCEEDED(hr))
        hr = TcOpsRun(Device, Result, TcOpsWaveMoreSource, "more", TcOpsWaveMoreBuilds, ARRAYSIZE(TcOpsWaveMoreBuilds), 2);
    return hr;
}

/* Views, a constant buffer and samplers picked from the descriptor heaps by values that differ across lanes */
static const char TcOpsHeapSource[] =
    "struct Pair { uint4 A; uint4 B; };\n"
    "[numthreads(64, 1, 1)] void heaps(uint t : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Base = t * 64;\n"
    "    Texture2D Picked = ResourceDescriptorHeap[NonUniformResourceIndex((t % 3 == 0) ? 0 : 9)];\n"
    "    SamplerState Which = SamplerDescriptorHeap[NonUniformResourceIndex((t % 3 == 1) ? 1 : ((t & 1) ? 3 : 0))];\n"
    "    ByteAddressBuffer Bytes = ResourceDescriptorHeap[3];\n"
    "    RWByteAddressBuffer Target = ResourceDescriptorHeap[4];\n"
    "    RWTexture2D<uint> Cells = ResourceDescriptorHeap[6];\n"
    "    ConstantBuffer<Pair> Values = ResourceDescriptorHeap[10];\n"
    "    float2 Uv = float2((t % 8) * 0.13, (t / 8) * 0.11);\n"
    "    uint w, h;\n"
    "    Out.Store4(Base, asuint(Picked.SampleLevel(Which, Uv, 0)));\n"
    "    Out.Store4(Base + 16, Bytes.Load4((t % 16) * 16));\n"
    "    Out.Store4(Base + 32, Values.A + Values.B * t);\n"
    "    Picked.GetDimensions(w, h);\n"
    "    Out.Store(Base + 48, w | (h << 16));\n"
    "    Target.Store(Base + 52, asuint(Picked.Load(int3(t % 4, t / 16, 0)).x));\n"
    "    InterlockedAdd(Cells[uint2(t % 8, (t / 8) % 8)], t);\n"
    "}\n";

static const TcOpsBuild TcOpsHeapBuilds[] =
{
    { NULL, L"cs_6_6", NULL },
};

static
HRESULT
SceneOpsHeap(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    return TcOpsRun(Device, Result, TcOpsHeapSource, "heaps", TcOpsHeapBuilds, ARRAYSIZE(TcOpsHeapBuilds), 1,
                    D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
}

/* Minimum precision and native 16 bit arithmetic, floats and both integer kinds */
static const char TcOpsHalfSource[] =
    "[numthreads(64, 1, 1)] void halves(uint t : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Base = t * 64;\n"
    "    min16float a = (min16float)(t * 0.37 + 0.1);\n"
    "    min16float b = (min16float)asfloat(Knob.w) / (min16float)(t + 3);\n"
    "    min16float c = a * b + (min16float)1.0 / (min16float)3.0;\n"
    "    min16uint u = (min16uint)(t * 4099 + Knob.y);\n"
    "    min16uint v = u * (min16uint)(t + 7) + (min16uint)60000;\n"
    "    min16int s = (min16int)(int)(t * 911) - (min16int)20000;\n"
    "    min16int r = (s * (min16int)3) >> 2;\n"
    "    Out.Store4(Base, uint4(asuint((float)a), asuint((float)b), asuint((float)c), (uint)v));\n"
    "    Out.Store4(Base + 16, uint4((uint)(int)r, (uint)(u >> 3), asuint((float)(a * a * a * (min16float)1000.0)), (uint)(int)((s ^ (min16int)0x7F3) + r)));\n"
    "    Out.Store4(Base + 32, uint4(asuint((float)sqrt(a)), asuint((float)frac(c * (min16float)17.0)), (uint)(min16uint)(c * (min16float)900.0), asuint((float)max(a, b))));\n"
    "    float One = (float)Knob.x / 3.0;\n"
    "    float Edge = (t % 6 == 0) ? 70000.0 + t : (t % 6 == 1) ? -65519.0 - t : (t % 6 == 2) ? 3.0e-6 * (t + 1) :\n"
    "                 (t % 6 == 3) ? -6.2e-5 / (t + 1) : (t % 6 == 4) ? asfloat(0x7F800000u + (t & 8) * 0x100000u) : 65504.0 - t * 0.25;\n"
    "    min16float e = (min16float)(Edge * One);\n"
    "    Out.Store4(Base + 48, uint4(asuint((float)e), asuint((float)(e * (min16float)2.0)), asuint((float)(e * (min16float)0.001)),\n"
    "                                asuint((float)(min16float)(uint)(t * 40503 * Knob.x))));\n"
    "}\n";

static const TcOpsBuild TcOpsHalfBuilds[] =
{
    { "cs_5_1", NULL, NULL },
    { NULL, L"cs_6_0", NULL },
    { NULL, L"cs_6_2", L"-enable-16bit-types" },
};

/* Native 16 bit constants, raw loads and stores at two byte steps, group shared and local 16 bit arrays */
static const char TcOpsShortSource[] =
    "groupshared float16_t Pool[66];\n"
    "groupshared uint16_t Counts[7];\n"
    "[numthreads(64, 1, 1)] void shorts(uint t : SV_DispatchThreadID, uint g : SV_GroupIndex)\n"
    "{\n"
    "    uint Base = t * 64;\n"
    "    float16_t h = (float16_t)(t * 0.731 - 9.0) * KnobHalves.y;\n"
    "    Out.Store4(Base, uint4(KnobShorts.x + KnobShorts.y * 65536u, asuint((float)KnobHalves.x), asuint((float)h), (uint)(int)(KnobSigned.w + KnobSigned.z)));\n"
    "    float16_t2 Pair = Raw.Load<float16_t2>((t * 6) % 250);\n"
    "    uint16_t4 Four = Raw.Load<uint16_t4>((t * 2) % 248);\n"
    "    int16_t Last = Raw.Load<int16_t>(254 - (t % 3) * 2);\n"
    "    Out.Store4(Base + 16, uint4(asuint((float)Pair.x), asuint((float)Pair.y), Four.x | ((uint)Four.w << 16), (uint)(int)Last));\n"
    "    Out.Store4(Base + 32, uint4(Four.y, Four.z, 0, 0));\n"
    "    Out.Store<float16_t>(Base + 32 + (t & 3) * 2, h);\n"
    "    Out.Store<uint16_t2>(Base + 42, uint16_t2(t * 977, Four.z + 1));\n"
    "    Pool[g] = h;\n"
    "    if (g < 2)\n"
    "        Pool[64 + g] = (float16_t)g;\n"
    "    if (g < 7)\n"
    "        Counts[g] = 0;\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    float16_t Row[5];\n"
    "    [loop] for (uint i = 0; i < 5; i++)\n"
    "        Row[i] = h * (float16_t)i;\n"
    "    Row[t % 5] += (float16_t)1.5;\n"
    "    Out.Store4(Base + 48, uint4(asuint((float)Pool[(g * 7 + 3) % 66]), asuint((float)Row[(t * 3) % 5]), asuint((float)Pool[65 - (g & 1)]), Counts[g % 7]));\n"
    "}\n";

static const TcOpsBuild TcOpsShortBuilds[] =
{
    { NULL, L"cs_6_2", L"-enable-16bit-types", L"SHORT_KNOBS" },
    { NULL, L"cs_6_6", L"-enable-16bit-types", L"SHORT_KNOBS" },
};

/* Texture loads and samples returning minimum precision and native 16 bit floats */
static const char TcOpsHalfReadSource[] =
    "[numthreads(64, 1, 1)] void halfreads(uint t : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Base = t * 64;\n"
    "    float2 Uv = float2((t % 8) * 0.137 - 0.2, (t / 8) * 0.161 - 0.3);\n"
    "    Out.Store4(Base, asuint((float4)Image.Load(int3(t % 8, t / 8, 0))));\n"
    "    Out.Store4(Base + 16, asuint((float4)Image.SampleLevel(Smooth, Uv, (t % 5) * 0.3)));\n"
    "    Out.Store4(Base + 32, asuint((float4)Image.GatherGreen(Smooth, Uv)));\n"
    "    Out.Store4(Base + 48, uint4(asuint((float)Floats.SampleLevel(Smooth, Uv, 0)), asuint((float)Floats.Load(int3(t % 4, t / 16, 0))),\n"
    "                                asuint((float)(Image.Load(int3(t % 8, 1, 0)).x * (min16float)3.0)), asuint((float)Floats.Gather(Point, Uv).w)));\n"
    "}\n";

static const TcOpsBuild TcOpsHalfReadBuilds[] =
{
    { NULL, L"cs_6_0", NULL, L"HALF_IMAGES" },
    { NULL, L"cs_6_2", L"-enable-16bit-types", L"HALF_IMAGES" },
};
static
HRESULT
SceneOpsHalf(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    HRESULT hr = TcOpsRun(Device, Result, TcOpsHalfSource, "halves", TcOpsHalfBuilds, ARRAYSIZE(TcOpsHalfBuilds), 1);

    if (SUCCEEDED(hr))
        hr = TcOpsRun(Device, Result, TcOpsShortSource, "shorts", TcOpsShortBuilds, ARRAYSIZE(TcOpsShortBuilds), 1);
    if (SUCCEEDED(hr))
        hr = TcOpsRun(Device, Result, TcOpsHalfReadSource, "halfreads", TcOpsHalfReadBuilds, ARRAYSIZE(TcOpsHalfReadBuilds), 1);
    return hr;
}

/* Doubles and 64 bit integers: arithmetic, conversions, memory, and wave operations on them */
static const char TcOpsWideSource[] =
    "groupshared double Shared[64];\n"
    "groupshared uint64_t Longs[16];\n"
    "[numthreads(64, 1, 1)] void wides(uint t : SV_DispatchThreadID, uint g : SV_GroupIndex)\n"
    "{\n"
    "    uint Base = t * 256;\n"
    "    double a = (double)t * 0.37 + 0.1;\n"
    "    double b = asdouble(Knob.y, Knob.w) / (double)(t + 3);\n"
    "    double c = fma(a, b, (double)1.0 / 3.0);\n"
    "    double d = a * b + c - (double)Knob.x;\n"
    "    double e = mad(a, -b, c);\n"
    "    uint lo, hi;\n"
    "    asuint(c, lo, hi);\n"
    "    Out.Store<double>(Base, a);\n"
    "    Out.Store4(Base + 8, uint4(lo, hi, asuint((float)d), (uint)(int)(d * 1000.0)));\n"
    "    Out.Store4(Base + 24, uint4((uint)(c * 7.0), (a < b) | ((a >= c) << 1) | ((d == d) << 2) | ((a != b) << 3) | ((c > d) << 4) | ((b <= e) << 5),\n"
    "                                asuint((float)min(a, b)), asuint((float)max(c, d))));\n"
    "    Out.Store4(Base + 40, uint4(asuint((float)abs(-d)), asuint((float)saturate(c - 1.0)), asuint((float)rcp(a)), asuint((float)(a / c))));\n"
    "    Out.Store<double>(Base + 56, e);\n"
    "    uint64_t u = (uint64_t)t * 0x9E3779B97F4A7C15ull + Knob.y;\n"
    "    int64_t s = (int64_t)(int)(t * 911) - 20000;\n"
    "    uint64_t v = (u >> (t % 61)) ^ (u << 7) | ((uint64_t)Knob.z << 40);\n"
    "    int64_t r = (s * 3) >> 2;\n"
    "    Out.Store4(Base + 64, uint4((uint)u, (uint)(u >> 32), (uint)v, (uint)(v >> 32)));\n"
    "    Out.Store4(Base + 80, uint4((uint)r, (uint)(r >> 32), (uint)(u / (t % 9)), (uint)((u % ((uint64_t)(t % 5) << 33)) >> 3)));\n"
    "    Out.Store4(Base + 96, uint4((u < v) | ((s < r) << 1) | ((u == v) << 2) | ((s >= r) << 3) | ((u > v) << 4), (uint)max(s, r),\n"
    "                                (uint)min(u, v), countbits(u) + firstbithigh(v) * 64 + firstbitlow(u) * 4096));\n"
    "    Out.Store4(Base + 112, uint4(asuint((float)u), asuint((float)s), (uint)(int64_t)(d * 1e9), (uint)((uint64_t)(a * 1e12) >> 20)));\n"
    "    Out.Store<double>(Base + 128, (double)s * a);\n"
    "    Out.Store<double>(Base + 136, (double)u);\n"
    "    Out.Store<int64_t>(Base + 144, (s * 3) / (int64_t)((int)(t % 16) - 7) + (s % (int64_t)((int)(t % 6) - 2)) * 977);\n"
    "    Out.Store<int64_t>(Base + 152, (int64_t)((float)a * -1e10f) + (int64_t)(uint64_t)((float)u));\n"
    "    uint64_t w = Raw.Load<uint64_t>((t * 8) % 240);\n"
    "    double x = Raw.Load<double>((t * 8) % 232 + 8);\n"
    "    Out.Store<uint64_t>(Base + 160, w + (uint64_t)asuint((float)x));\n"
    "    double Table[6];\n"
    "    [loop] for (uint i = 0; i < 6; i++)\n"
    "        Table[i] = a * i + b;\n"
    "    Table[(t * 5) % 6] += c;\n"
    "    Shared[g] = d;\n"
    "    if (g < 16)\n"
    "        Longs[g] = u;\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    Out.Store<double>(Base + 168, Table[t % 6] + Shared[63 - g]);\n"
    "    Out.Store<uint64_t>(Base + 176, Longs[(g * 3) % 16] ^ v);\n"
    "    Out.Store<double>(Base + 184, WaveActiveSum(a));\n"
    "    Out.Store<double>(Base + 192, WavePrefixProduct(b));\n"
    "    Out.Store<double>(Base + 200, WaveActiveMin(d) + WaveActiveMax(c));\n"
    "    Out.Store<uint64_t>(Base + 208, WaveActiveSum(u) ^ WavePrefixSum(v));\n"
    "    Out.Store<int64_t>(Base + 216, WaveActiveMin(s) + WaveActiveMax(r) * 3);\n"
    "    Out.Store<uint64_t>(Base + 224, WaveActiveBitXor(u) | (WaveActiveMax(v) & 0xFF));\n"
    "    Out.Store<uint64_t>(Base + 232, WaveReadLaneAt(u, t % 3) + QuadReadAcrossX(v) + WaveReadLaneFirst(u >> 3));\n"
    "    Out.Store2(Base + 240, uint2(WaveActiveAllEqual(u >> 2) | (WaveActiveAllEqual(a > 2.0 ? 1.0 : 0.0) << 1), (uint)(QuadReadAcrossDiagonal(c) * 100.0)));\n"
    "    if (t % 3 != 0)\n"
    "    {\n"
    "        Out.Store<double>(Base + 248, WaveActiveSum(c) + WavePrefixSum(d));\n"
    "        uint64_t First = WaveReadLaneFirst(u);\n"
    "        asuint(WaveReadLaneFirst(a), lo, hi);\n"
    "        Counted[t] = (uint)First;\n"
    "        Typed[t] = (uint)(First >> 32);\n"
    "        Grid[uint2(t % 8, t / 8)] = lo ^ hi;\n"
    "    }\n"
    "}\n";

static const TcOpsBuild TcOpsWideBuilds[] =
{
    { NULL, L"cs_6_0", NULL },
    { NULL, L"cs_6_6", NULL },
};

static
HRESULT
SceneOpsWide(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    return TcOpsRun(Device, Result, TcOpsWideSource, "wides", TcOpsWideBuilds, ARRAYSIZE(TcOpsWideBuilds), 1);
}


/*
 * Pixel shaders built by DXC over a four sample target: system values per pixel and per
 * sample, front and back faces, primitive IDs, and wave operations around helper lanes.
 * Each invocation writes one record at its pixel and sample.
 */
static const char TcOpsPixelSource[] =
    "RWByteAddressBuffer Out : register(u0);\n"
    "struct VsOut { float4 Pos : SV_Position; float4 Color : COLOR; };\n"
    "VsOut vs(float2 Pos : POSITION, float4 Color : COLOR, uint Id : SV_VertexID)\n"
    "{\n"
    "    VsOut o;\n"
    "    o.Pos = float4(Pos.x, (Id >= 3) ? -Pos.y : Pos.y, 0.5, 1);\n"
    "    o.Color = Color;\n"
    "    return o;\n"
    "}\n"
    "uint Record(float4 Pos, uint Sample) { return (((uint)Pos.y * 16 + (uint)Pos.x) * 4 + Sample) * 32; }\n"
    "float4 pixels(VsOut i, uint Coverage : SV_Coverage, bool Front : SV_IsFrontFace, uint Primitive : SV_PrimitiveID) : SV_Target\n"
    "{\n"
    "    uint At = Record(i.Pos, 0);\n"
    "    uint Lane = WaveGetLaneIndex();\n"
    "    float Value = i.Color.x * 100.0 + i.Pos.x;\n"
    "    Out.Store4(At, uint4(Coverage | (Front ? 0x100 : 0) | (Primitive << 16), asuint(i.Pos.x), asuint(i.Pos.y), asuint(i.Pos.z)));\n"
    "    Out.Store4(At + 16, uint4(WaveActiveCountBits(true) | (Lane << 8) | (WaveIsFirstLane() << 12) | (WavePrefixCountBits(true) << 16),\n"
    "                              asuint(WaveActiveSum(Value)), asuint(QuadReadAcrossX(Value)), WaveActiveBallot(true).x | (QuadReadAcrossY(Lane) << 8) |\n"
    "                              (WaveReadLaneFirst(Lane) << 16) | (QuadReadAcrossDiagonal(IsHelperLane() ? 1u : 0u) << 24)));\n"
    "    return i.Color;\n"
    "}\n"
    "float4 samples(VsOut i, uint Sample : SV_SampleIndex, uint Coverage : SV_Coverage, bool Front : SV_IsFrontFace) : SV_Target\n"
    "{\n"
    "    uint At = Record(i.Pos, Sample);\n"
    "    Out.Store4(At, uint4(Coverage | (Front ? 0x100 : 0) | (Sample << 12), asuint(i.Pos.x), asuint(i.Pos.y), asuint(i.Color.y)));\n"
    "    Out.Store4(At + 16, uint4(WaveActiveCountBits(true), asuint(WavePrefixSum(i.Color.x)), WaveActiveBallot(true).x, QuadReadAcrossX(Sample)));\n"
    "    return i.Color;\n"
    "}\n";

/* Pixel shader system values and waves over four samples, per pixel and per sample */
static
HRESULT
SceneOpsPixel(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const TcVertex Vertices[] =
    {
        { { -0.9f, -0.8f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.1f, 0.95f }, { 0.0f, 1.0f, 0.0f, 0.5f } },
        { { 0.85f, -0.7f }, { 0.0f, 0.0f, 1.0f, 0.25f } },
        { { -0.6f, 0.3f }, { 1.0f, 1.0f, 0.0f, 0.75f } },
        { { 0.7f, 0.45f }, { 0.0f, 1.0f, 1.0f, 1.0f } },
        { { -0.2f, -0.95f }, { 1.0f, 0.0f, 1.0f, 0.6f } },
    };
    static const PCSTR Entries[] = { "pixels", "samples" };
    static const UINT Size = 16 * 16 * 4 * 32;
    SIZE_T LogStart = TcLogMark();
    D3D12_ROOT_PARAMETER Parameter = {};
    D3D12_ROOT_SIGNATURE_DESC Signature = {};
    D3D12_VERTEX_BUFFER_VIEW View = {};
    D3D12_VIEWPORT Viewport = { 0.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f };
    D3D12_RECT Scissor = { 0, 0, 16, 16 };
    ID3D12RootSignature *Root = NULL;
    ID3D12Resource *Target;
    ID3D12Resource *Records;
    ID3D12Resource *Readback;
    ID3D12Resource *Geometry;
    ID3D12DescriptorHeap *Rtvs;
    ID3DBlob *Vertex = NULL;
    TcDrawKit Kit;
    TcQueue Queue;
    HRESULT hr;

    Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    Parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Signature.NumParameters = 1;
    Signature.pParameters = &Parameter;
    Signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = TcRootSignature(Device, &Signature, &Root);
    Kit.Keep(Root);
    if (SUCCEEDED(hr))
        hr = TcDxcCompile(TcOpsPixelSource, L"vs", L"vs_6_0", NULL, NULL, &Vertex);
    Kit.Keep(Vertex);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;

    Target = Kit.Keep(TcTarget(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 4, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET));
    Readback = Kit.Keep(TcBuffer(Device, D3D12_HEAP_TYPE_READBACK, Size, D3D12_RESOURCE_FLAG_NONE));
    Geometry = Kit.Keep(TcUpload(Device, Vertices, sizeof(Vertices)));
    Rtvs = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, FALSE, &Kit);
    if (Target == NULL || Readback == NULL || Geometry == NULL || Rtvs == NULL)
    {
        hr = E_OUTOFMEMORY;
        TcAppend(Result, &hr, sizeof(hr));
        return hr;
    }
    Device->CreateRenderTargetView(Target, NULL, Rtvs->GetCPUDescriptorHandleForHeapStart());
    View.BufferLocation = Geometry->GetGPUVirtualAddress();
    View.SizeInBytes = sizeof(Vertices);
    View.StrideInBytes = sizeof(TcVertex);

    for (UINT Index = 0; Index < ARRAYSIZE(Entries); Index++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE Rtv = Rtvs->GetCPUDescriptorHandleForHeapStart();
        std::wstring Entry(Entries[Index], Entries[Index] + strlen(Entries[Index]));
        D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc;
        ID3D12PipelineState *State = NULL;
        ID3DBlob *Pixel = NULL;

        hr = TcDxcCompile(TcOpsPixelSource, Entry.c_str(), L"ps_6_6", NULL, NULL, &Pixel);
        Kit.Keep(Pixel);
        if (SUCCEEDED(hr))
        {
            Desc = TcPipelineDesc(Root, Vertex, Pixel);
            Desc.SampleDesc.Count = 4;
            State = TcPipeline(Device, Desc, &Kit);
            hr = (State != NULL) ? S_OK : E_FAIL;
        }
        TcAppend(Result, &hr, sizeof(hr));
        if (FAILED(hr))
            continue;
        /* A fresh buffer per pipeline starts out zero */
        Records = Kit.Keep(TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, Size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
        if (Records == NULL)
            return E_OUTOFMEMORY;
        TcBarrier(Queue.List, Records, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Queue.List->OMSetRenderTargets(1, &Rtv, FALSE, NULL);
        Queue.List->SetGraphicsRootSignature(Root);
        Queue.List->SetGraphicsRootUnorderedAccessView(0, Records->GetGPUVirtualAddress());
        Queue.List->IASetVertexBuffers(0, 1, &View);
        Queue.List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Queue.List->RSSetViewports(1, &Viewport);
        Queue.List->RSSetScissorRects(1, &Scissor);
        Queue.List->SetPipelineState(State);
        Queue.List->DrawInstanced(ARRAYSIZE(Vertices), 1, 0, 0);
        TcBarrier(Queue.List, Records, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Queue.List->CopyBufferRegion(Readback, 0, Records, 0, Size);
        hr = Queue.Submit();
        TcAppend(Result, &hr, sizeof(hr));
        if (FAILED(hr))
            return hr;
        TcReadback(Readback, Size, Result);
    }
    TcLogSince(LogStart, Result);
    return hr;
}