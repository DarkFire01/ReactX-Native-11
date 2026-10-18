/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Newer device, context and DXGI interfaces, queries and multithread protection
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "check.hpp"

#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <stdio.h>

/* interfaces: which newer interfaces answer QueryInterface */

static
VOID
AcCheckInterfaces(
    _In_ CheckContext& Check)
{
    static const struct
    {
        PCSTR Name;
        GUID Id;
        BOOLEAN OnContext;
    } Probes[] =
    {
        { "device1", __uuidof(ID3D11Device1), FALSE },
        { "device2", __uuidof(ID3D11Device2), FALSE },
        { "multithread10", __uuidof(ID3D10Multithread), FALSE },
        { "dxgidevice2", __uuidof(IDXGIDevice2), FALSE },
        { "dxgidevice3", __uuidof(IDXGIDevice3), FALSE },
        { "context1", __uuidof(ID3D11DeviceContext1), TRUE },
        { "context2", __uuidof(ID3D11DeviceContext2), TRUE },
        { "multithread11", __uuidof(ID3D11Multithread), TRUE },
    };
    ComPtr<ID3D10Multithread> Multithread;
    ComPtr<ID3D11Multithread> ContextMultithread;
    ComPtr<IUnknown> Found;
    char Key[64];
    HRESULT hr;

    for (const auto& Probe : Probes)
    {
        IUnknown* Owner = Probe.OnContext ? static_cast<IUnknown*>(Check.Context) : static_cast<IUnknown*>(Check.Device);

        hr = Owner->QueryInterface(Probe.Id, reinterpret_cast<void**>(Found.ReleaseAndGetAddressOf()));
        sprintf_s(Key, "qi.%s", Probe.Name);
        Check.Report(Key, "0x%08lX", hr);
    }

    /* Protection starts off and both interfaces share it */
    if (SUCCEEDED(Check.Device->QueryInterface(IID_PPV_ARGS(&Multithread))) &&
        SUCCEEDED(Check.Context->QueryInterface(IID_PPV_ARGS(&ContextMultithread))))
    {
        Check.Report("multithread.default", "%d", Multithread->GetMultithreadProtected());
        Check.Report("multithread.set", "%d", Multithread->SetMultithreadProtected(TRUE));
        Check.Report("multithread.shared", "%d", ContextMultithread->GetMultithreadProtected());
        Check.Report("multithread.reset", "%d", ContextMultithread->SetMultithreadProtected(FALSE));
        Multithread->Enter();
        ContextMultithread->Enter();
        ContextMultithread->Leave();
        Multithread->Leave();
    }
}

/* states1: the *1 state descs round trip and validate like Windows */

static
VOID
AcCheckStates1(
    _In_ CheckContext& Check)
{
    ComPtr<ID3D11Device1> Device1;
    ComPtr<ID3D11BlendState1> Blend;
    ComPtr<ID3D11BlendState> BlendBase;
    ComPtr<ID3D11RasterizerState1> Rasterizer;
    D3D11_BLEND_DESC1 Desc = {};
    D3D11_BLEND_DESC1 Back = {};
    D3D11_RASTERIZER_DESC1 Raster = {};
    D3D11_RASTERIZER_DESC1 RasterBack = {};
    HRESULT hr;

    if (FAILED(Check.Device->QueryInterface(IID_PPV_ARGS(&Device1))))
        return;

    Desc.RenderTarget[0].LogicOpEnable = TRUE;
    Desc.RenderTarget[0].LogicOp = D3D11_LOGIC_OP_XOR;
    Desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    Desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    Desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    Desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    Desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    Desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    Desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = Device1->CreateBlendState1(&Desc, &Blend);
    Check.Report("blend1.logic", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Blend->GetDesc1(&Back);
        Check.Report("blend1.back", "%d %d %d", Back.RenderTarget[0].LogicOpEnable, Back.RenderTarget[0].LogicOp,
                     Back.RenderTarget[1].LogicOp);
        Check.Report("blend1.base", "0x%08lX", Blend.As(&BlendBase));
    }

    Desc.RenderTarget[0].BlendEnable = TRUE;
    Check.Report("blend1.both", "0x%08lX", Device1->CreateBlendState1(&Desc, nullptr));
    Desc.RenderTarget[0].BlendEnable = FALSE;
    Desc.IndependentBlendEnable = TRUE;
    Check.Report("blend1.independent", "0x%08lX", Device1->CreateBlendState1(&Desc, nullptr));

    Raster.FillMode = D3D11_FILL_SOLID;
    Raster.CullMode = D3D11_CULL_BACK;
    Raster.DepthClipEnable = TRUE;
    Raster.ForcedSampleCount = 4;
    hr = Device1->CreateRasterizerState1(&Raster, &Rasterizer);
    Check.Report("raster1.forced", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Rasterizer->GetDesc1(&RasterBack);
        Check.Report("raster1.back", "%u", RasterBack.ForcedSampleCount);
    }

    Raster.ForcedSampleCount = 3;
    Check.Report("raster1.three", "0x%08lX", Device1->CreateRasterizerState1(&Raster, nullptr));
    Raster.ForcedSampleCount = 4;
    Raster.MultisampleEnable = TRUE;
    Check.Report("raster1.withmsaa", "0x%08lX", Device1->CreateRasterizerState1(&Raster, nullptr));
}

/* ranges: constant buffer ranges set and read back */

static
VOID
AcCheckRanges(
    _In_ CheckContext& Check)
{
    ComPtr<ID3D11DeviceContext1> Context1;
    ComPtr<ID3D11Buffer> Buffer;
    ID3D11Buffer* Back = nullptr;
    D3D11_BUFFER_DESC Desc = {};
    UINT First = 32;
    UINT Count = 64;
    UINT BadFirst = 8;
    UINT FirstBack = 0xCDCD;
    UINT CountBack = 0xCDCD;
    ID3D11Buffer* Bind;

    if (FAILED(Check.Context->QueryInterface(IID_PPV_ARGS(&Context1))))
        return;

    Desc.ByteWidth = 4096;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(Check.Device->CreateBuffer(&Desc, nullptr, &Buffer)))
        return;

    Bind = Buffer.Get();
    Context1->VSSetConstantBuffers1(0, 1, &Bind, &First, &Count);
    Context1->VSGetConstantBuffers1(0, 1, &Back, &FirstBack, &CountBack);
    Check.Report("ranges.set", "%d %u %u", Back == Buffer.Get(), FirstBack, CountBack);
    if (Back != nullptr)
        Back->Release();

    /* A misaligned first constant leaves the old binding */
    Context1->VSSetConstantBuffers1(0, 1, &Bind, &BadFirst, &Count);
    Context1->VSGetConstantBuffers1(0, 1, &Back, &FirstBack, &CountBack);
    Check.Report("ranges.misaligned", "%d %u %u", Back == Buffer.Get(), FirstBack, CountBack);
    if (Back != nullptr)
        Back->Release();

    /* The old setter goes back to whole buffers */
    Check.Context->VSSetConstantBuffers(0, 1, &Bind);
    Context1->VSGetConstantBuffers1(0, 1, &Back, &FirstBack, &CountBack);
    Check.Report("ranges.whole", "%d %u %u", Back == Buffer.Get(), FirstBack, CountBack);
    if (Back != nullptr)
        Back->Release();

    Bind = nullptr;
    Check.Context->VSSetConstantBuffers(0, 1, &Bind);
}

/* queries: creation rules and an event query that completes */

static
VOID
AcCheckQueries(
    _In_ CheckContext& Check)
{
    ComPtr<ID3D11Query> Event;
    ComPtr<ID3D11Predicate> Predicate;
    D3D11_QUERY_DESC Desc = {};
    BOOL Done = FALSE;
    UINT64 Samples = 0;
    char Key[64];
    HRESULT hr;
    UINT Kind;
    UINT Spins = 0;

    for (Kind = 0; Kind <= D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3 + 1; Kind++)
    {
        ComPtr<ID3D11Query> Query;

        Desc.Query = (D3D11_QUERY)Kind;
        hr = Check.Device->CreateQuery(&Desc, &Query);
        sprintf_s(Key, "query%u", Kind);
        Check.Report(Key, "0x%08lX size %u", hr, SUCCEEDED(hr) ? Query->GetDataSize() : 0);

        sprintf_s(Key, "predicate%u", Kind);
        Check.Report(Key, "0x%08lX", Check.Device->CreatePredicate(&Desc, nullptr));
    }

    Desc.Query = D3D11_QUERY_EVENT;
    Desc.MiscFlags = D3D11_QUERY_MISC_PREDICATEHINT;
    Check.Report("query.eventhint", "0x%08lX", Check.Device->CreateQuery(&Desc, nullptr));
    Desc.MiscFlags = 2;
    Check.Report("query.badmisc", "0x%08lX", Check.Device->CreateQuery(&Desc, nullptr));
    Desc.MiscFlags = 0;

    if (FAILED(Check.Device->CreateQuery(&Desc, &Event)))
        return;

    Check.Report("event.beforeend", "0x%08lX", Check.Context->GetData(Event.Get(), &Done, sizeof(Done), 0));
    Check.Context->End(Event.Get());
    Check.Report("event.badsize", "0x%08lX", Check.Context->GetData(Event.Get(), &Done, 8, 0));
    while ((hr = Check.Context->GetData(Event.Get(), &Done, sizeof(Done), 0)) == S_FALSE && Spins < 1000000)
        Spins++;
    Check.Report("event.done", "0x%08lX %d", hr, Done);

    /* An occlusion query around no draws counts nothing */
    Desc.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
    if (SUCCEEDED(Check.Device->CreatePredicate(&Desc, &Predicate)))
    {
        Check.Context->Begin(Predicate.Get());
        Check.Context->End(Predicate.Get());
        Spins = 0;
        while ((hr = Check.Context->GetData(Predicate.Get(), &Done, sizeof(Done), 0)) == S_FALSE && Spins < 1000000)
            Spins++;
        Check.Report("predicate.empty", "0x%08lX %d", hr, Done);
        Check.Context->SetPredication(Predicate.Get(), FALSE);

        ID3D11Predicate* Bound = nullptr;
        BOOL Value = TRUE;

        Check.Context->GetPredication(&Bound, &Value);
        Check.Report("predicate.bound", "%d %d", Bound == Predicate.Get(), Value);
        if (Bound != nullptr)
            Bound->Release();
        Check.Context->SetPredication(nullptr, FALSE);
    }

    UNREFERENCED_PARAMETER(Samples);
}

static const CheckInfo InterfaceChecks[] =
{
    { "interfaces", AcCheckInterfaces },
    { "states1", AcCheckStates1 },
    { "ranges", AcCheckRanges },
    { "queries", AcCheckQueries },
};

const CheckInfo*
AcInterfaceChecks(
    _Out_ UINT* Count)
{
    *Count = _countof(InterfaceChecks);
    return InterfaceChecks;
}
