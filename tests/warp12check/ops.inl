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

    return TcDxcCompile(Source, Wide.c_str(), Build.DxcProfile, Build.DxcExtra, Blob);
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
};

static const char TcOpsCommon[] =
    "cbuffer Knobs : register(b0) { uint4 Knob; };\n"
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
    _In_ UINT Groups)
{
    static const UINT Constants[4] = { 3, 0x9E3779B9u, 5, 0x40490FDBu };
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcOpsKit Ops;
    TcQueue Queue;
    HRESULT hr;

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
