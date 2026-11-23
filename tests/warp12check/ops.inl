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

/* One way to build a program: FXC bytecode the runtime converts, or DXC's own DXIL */
struct TcOpsBuild
{
    PCSTR FxcProfile;
    PCWSTR DxcProfile;
    PCWSTR DxcExtra;
};

static const TcOpsBuild TcOpsBoth[] =
{
    { "cs_5_1", NULL, NULL },
    { NULL, L"cs_6_0", NULL },
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
    static constexpr UINT OutputSize = 4096;
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
            memcpy(Data + 2 * 65536 + 8192, &Five, sizeof(Five));
            Staging->Unmap(0, NULL);
        }
        Queue->List->CopyBufferRegion(Output, 0, Staging, 2 * 65536, OutputSize);
        Queue->List->CopyBufferRegion(Counted, 0, Staging, 3 * 65536, 256);
        Queue->List->CopyBufferRegion(Typed, 0, Staging, 3 * 65536 + 4, 252);
        Queue->List->CopyBufferRegion(Counter, 0, Staging, 2 * 65536 + 8192, 4);
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
    "Texture2D<float> SpaceFloats : register(t0, space1);\n"
    "Texture2D SpaceImage : register(t1, space1);\n"
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
    return TcOpsRun(Device, Result, TcOpsFlowSource, "flow", TcOpsBoth, ARRAYSIZE(TcOpsBoth), 2);
}
