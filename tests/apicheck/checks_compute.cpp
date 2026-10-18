/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compute, unordered access, stream output and binding hazard checks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "check.hpp"

#include <vector>

#include "SimpleVS.h"
#include "SimplePS.h"
#include "SquaresCS.h"
#include "AppendCS.h"
#include "PassGS.h"
#include "PassHS.h"
#include "PassDS.h"

constexpr UINT AcElementCount = 256;

struct AcParameters
{
    UINT Add;
    UINT Every;
    UINT Unused[2];
};

static const D3D11_INPUT_ELEMENT_DESC AcSimpleLayout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static
ComPtr<ID3D11Buffer>
AcCreateBuffer(
    _In_ CheckContext& Check,
    _In_ UINT BindFlags,
    _In_ UINT Size,
    _In_ UINT MiscFlags = 0,
    _In_ UINT Stride = 0,
    _In_opt_ const VOID* Data = nullptr)
{
    D3D11_BUFFER_DESC Desc = {};
    D3D11_SUBRESOURCE_DATA Initial = {};
    ComPtr<ID3D11Buffer> Buffer;

    Desc.ByteWidth = Size;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = BindFlags;
    Desc.MiscFlags = MiscFlags;
    Desc.StructureByteStride = Stride;
    Initial.pSysMem = Data;
    Check.Expect(SUCCEEDED(Check.Device->CreateBuffer(&Desc, Data ? &Initial : nullptr, &Buffer)), "CreateBuffer");
    return Buffer;
}

static
ComPtr<ID3D11Texture2D>
AcCreateTexture(
    _In_ CheckContext& Check,
    _In_ UINT BindFlags)
{
    D3D11_TEXTURE2D_DESC Desc = {};
    ComPtr<ID3D11Texture2D> Texture;

    Desc.Width = 16;
    Desc.Height = 16;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BindFlags = BindFlags;
    Check.Expect(SUCCEEDED(Check.Device->CreateTexture2D(&Desc, nullptr, &Texture)), "CreateTexture2D");
    return Texture;
}

/* Copies a buffer to a staging one and hands back its bytes */
static
std::vector<BYTE>
AcReadBuffer(
    _In_ CheckContext& Check,
    _In_ ID3D11Buffer* Buffer)
{
    D3D11_BUFFER_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    ComPtr<ID3D11Buffer> Staging;
    std::vector<BYTE> Bytes;

    Buffer->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.MiscFlags = 0;
    Desc.StructureByteStride = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (!Check.Expect(SUCCEEDED(Check.Device->CreateBuffer(&Desc, nullptr, &Staging)), "staging buffer"))
        return Bytes;

    Check.Context->CopyResource(Staging.Get(), Buffer);
    if (!Check.Expect(SUCCEEDED(Check.Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped)), "Map staging"))
        return Bytes;

    Bytes.assign(static_cast<BYTE*>(Mapped.pData), static_cast<BYTE*>(Mapped.pData) + Desc.ByteWidth);
    Check.Context->Unmap(Staging.Get(), 0);
    return Bytes;
}

static
UINT
AcReadUint(
    _In_ const std::vector<BYTE>& Bytes,
    _In_ UINT Index)
{
    UINT Value = 0;

    if ((Index + 1) * sizeof(UINT) <= Bytes.size())
        memcpy(&Value, &Bytes[Index * sizeof(UINT)], sizeof(Value));
    return Value;
}

static
UINT
AcCountNonZero(
    _In_ const std::vector<BYTE>& Bytes)
{
    UINT Count = 0;
    UINT Index;

    for (Index = 0; Index < Bytes.size() / sizeof(UINT); Index++)
        Count += (AcReadUint(Bytes, Index) != 0);
    return Count;
}

/* Reports what a getter returned for one slot against what was bound there, then drops it */
template <typename Interface>
static
VOID
AcSlot(
    _In_ CheckContext& Check,
    _In_z_ PCSTR Key,
    _In_opt_ Interface* Bound,
    _In_opt_ Interface* Returned)
{
    Check.Report(Key, "%s", Returned == nullptr ? "null" : (Returned == Bound ? "same" : "other"));
    if (Returned != nullptr)
        Returned->Release();
}

static
VOID
AcSetParameters(
    _In_ CheckContext& Check,
    _In_ ID3D11Buffer* Constants,
    _In_ UINT Add,
    _In_ UINT Every)
{
    AcParameters Parameters = { Add, Every, { 0, 0 } };

    Check.Context->UpdateSubresource(Constants, 0, nullptr, &Parameters, 0, 0);
}

/* compute: dispatch, UAV clears and indirect dispatch, read back */

static
VOID
AcCheckCompute(
    _In_ CheckContext& Check)
{
    static const UINT Zero[4] = {};
    static const UINT Groups[6] = { 2, 1, 1, 0, 0, 0 };
    ComPtr<ID3D11Buffer> Squares = AcCreateBuffer(Check, D3D11_BIND_UNORDERED_ACCESS, AcElementCount * sizeof(UINT),
                                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(UINT));
    ComPtr<ID3D11Buffer> Constants = AcCreateBuffer(Check, D3D11_BIND_CONSTANT_BUFFER, sizeof(AcParameters));
    ComPtr<ID3D11Buffer> Arguments = AcCreateBuffer(Check, 0, sizeof(Groups), D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS,
                                                    0, Groups);
    ComPtr<ID3D11Buffer> PlainArguments = AcCreateBuffer(Check, D3D11_BIND_VERTEX_BUFFER, sizeof(Groups), 0, 0, Groups);
    ComPtr<ID3D11UnorderedAccessView> View;
    ComPtr<ID3D11ComputeShader> Shader;
    std::vector<BYTE> Bytes;
    ULONGLONG Sum = 0;
    UINT Index;
    HRESULT hr;

    hr = Check.Device->CreateUnorderedAccessView(Squares.Get(), nullptr, &View);
    Check.Report("default_uav", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Check.Expect(SUCCEEDED(Check.Device->CreateComputeShader(g_SquaresCS, sizeof(g_SquaresCS), nullptr, &Shader)),
                 "CreateComputeShader");
    AcSetParameters(Check, Constants.Get(), 7, 1);
    Check.Context->CSSetShader(Shader.Get(), nullptr, 0);
    Check.Context->CSSetConstantBuffers(0, 1, Constants.GetAddressOf());
    Check.Context->CSSetUnorderedAccessViews(0, 1, View.GetAddressOf(), nullptr);
    Check.Context->Dispatch(AcElementCount / 64, 1, 1);

    Bytes = AcReadBuffer(Check, Squares.Get());
    for (Index = 0; Index < AcElementCount; Index++)
        Sum += AcReadUint(Bytes, Index);
    Check.Report("dispatch_sum", "%llu", Sum);
    Check.Report("dispatch_last", "%u", AcReadUint(Bytes, AcElementCount - 1));
    Check.Expect(AcReadUint(Bytes, 10) == 107, "thread 10 wrote 10*10+7");

    Check.Context->ClearUnorderedAccessViewUint(View.Get(), Zero);
    Check.Report("cleared_nonzero", "%u", AcCountNonZero(AcReadBuffer(Check, Squares.Get())));

    /* Two groups of 64 from the argument buffer */
    Check.Context->DispatchIndirect(Arguments.Get(), 0);
    Check.Report("indirect_nonzero", "%u", AcCountNonZero(AcReadBuffer(Check, Squares.Get())));

    /* Misaligned, past the end and missing the indirect flag are all dropped */
    Check.Context->ClearUnorderedAccessViewUint(View.Get(), Zero);
    Check.Context->DispatchIndirect(Arguments.Get(), 2);
    Check.Context->DispatchIndirect(Arguments.Get(), 16);
    Check.Context->DispatchIndirect(PlainArguments.Get(), 0);
    Check.Report("bad_indirect_nonzero", "%u", AcCountNonZero(AcReadBuffer(Check, Squares.Get())));
}

/* append: hidden counters, initial counts and CopyStructureCount */

static
VOID
AcCheckAppend(
    _In_ CheckContext& Check)
{
    static const UINT Marker[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
    ComPtr<ID3D11Buffer> Picked = AcCreateBuffer(Check, D3D11_BIND_UNORDERED_ACCESS, AcElementCount * sizeof(UINT),
                                                 D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(UINT));
    ComPtr<ID3D11Buffer> Counts = AcCreateBuffer(Check, 0, sizeof(Marker), 0, 0, Marker);
    ComPtr<ID3D11Buffer> Constants = AcCreateBuffer(Check, D3D11_BIND_CONSTANT_BUFFER, sizeof(AcParameters));
    D3D11_UNORDERED_ACCESS_VIEW_DESC Desc = {};
    ComPtr<ID3D11UnorderedAccessView> Appending;
    ComPtr<ID3D11UnorderedAccessView> Plain;
    ComPtr<ID3D11ComputeShader> Shader;
    std::vector<BYTE> Bytes;
    UINT Initial;

    Desc.Format = DXGI_FORMAT_UNKNOWN;
    Desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    Desc.Buffer.NumElements = AcElementCount;
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(Picked.Get(), &Desc, &Plain)), "plain UAV");
    Desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(Picked.Get(), &Desc, &Appending)), "append UAV");
    Check.Expect(SUCCEEDED(Check.Device->CreateComputeShader(g_AppendCS, sizeof(g_AppendCS), nullptr, &Shader)),
                 "CreateComputeShader");
    if (Appending == nullptr || Shader == nullptr)
        return;

    AcSetParameters(Check, Constants.Get(), 0, 3);
    Check.Context->CSSetShader(Shader.Get(), nullptr, 0);
    Check.Context->CSSetConstantBuffers(0, 1, Constants.GetAddressOf());

    /* Start at zero, then at ten, then keep whatever the counter holds */
    Initial = 0;
    Check.Context->CSSetUnorderedAccessViews(1, 1, Appending.GetAddressOf(), &Initial);
    Check.Context->Dispatch(AcElementCount / 64, 1, 1);
    Check.Context->CopyStructureCount(Counts.Get(), 0, Appending.Get());

    Initial = 10;
    Check.Context->CSSetUnorderedAccessViews(1, 1, Appending.GetAddressOf(), &Initial);
    AcSetParameters(Check, Constants.Get(), 0, 16);
    Check.Context->Dispatch(AcElementCount / 64, 1, 1);
    Check.Context->CopyStructureCount(Counts.Get(), 4, Appending.Get());

    Check.Context->CSSetUnorderedAccessViews(1, 1, Appending.GetAddressOf(), nullptr);
    Check.Context->Dispatch(AcElementCount / 64, 1, 1);
    Check.Context->CopyStructureCount(Counts.Get(), 8, Appending.Get());

    /* A misaligned offset and a view without a counter are both dropped */
    Check.Context->CopyStructureCount(Counts.Get(), 14, Appending.Get());
    Check.Context->CopyStructureCount(Counts.Get(), 12, Plain.Get());

    Bytes = AcReadBuffer(Check, Counts.Get());
    Check.Report("count_from_zero", "%u", AcReadUint(Bytes, 0));
    Check.Report("count_from_ten", "%u", AcReadUint(Bytes, 1));
    Check.Report("count_kept", "%u", AcReadUint(Bytes, 2));
    Check.Report("count_dropped", "0x%08X", AcReadUint(Bytes, 3));
    Check.Expect(AcReadUint(Bytes, 0) == 86, "86 multiples of 3 below 256");
}

/* uavclear: float clears flush denormals, uint clears store the raw bits */

static
VOID
AcCheckUavClear(
    _In_ CheckContext& Check)
{
    static const UINT Bits[4] = { 1, 2, 3, 0xFFFFFFFF };
    const FLOAT Floats[4] = { 1.0e-40f, 1.5f, -0.0f, 3.0f };
    ComPtr<ID3D11Buffer> FloatBuffer = AcCreateBuffer(Check, D3D11_BIND_UNORDERED_ACCESS, 4 * sizeof(FLOAT));
    ComPtr<ID3D11Buffer> UintBuffer = AcCreateBuffer(Check, D3D11_BIND_UNORDERED_ACCESS, 4 * sizeof(UINT));
    ComPtr<ID3D11Texture2D> Texture = AcCreateTexture(Check, D3D11_BIND_UNORDERED_ACCESS);
    D3D11_UNORDERED_ACCESS_VIEW_DESC Desc = {};
    ComPtr<ID3D11UnorderedAccessView> FloatView;
    ComPtr<ID3D11UnorderedAccessView> UintView;
    ComPtr<ID3D11UnorderedAccessView> TextureView;
    D3D11_TEXTURE2D_DESC StagingDesc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    ComPtr<ID3D11Texture2D> Staging;
    const FLOAT Half[4] = { 0.5f, 0.25f, 1.0f / 3.0f, 1.0f };
    std::vector<BYTE> Bytes;

    Desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    Desc.Buffer.NumElements = 4;
    Desc.Format = DXGI_FORMAT_R32_FLOAT;
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(FloatBuffer.Get(), &Desc, &FloatView)), "float UAV");
    Desc.Format = DXGI_FORMAT_R32_UINT;
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(UintBuffer.Get(), &Desc, &UintView)), "uint UAV");
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(Texture.Get(), nullptr, &TextureView)),
                 "texture UAV");
    if (FloatView == nullptr || UintView == nullptr || TextureView == nullptr)
        return;

    Check.Context->ClearUnorderedAccessViewFloat(FloatView.Get(), Floats);
    Bytes = AcReadBuffer(Check, FloatBuffer.Get());
    Check.Report("float_bits", "%08X %08X %08X %08X", AcReadUint(Bytes, 0), AcReadUint(Bytes, 1), AcReadUint(Bytes, 2),
                 AcReadUint(Bytes, 3));

    Check.Context->ClearUnorderedAccessViewUint(UintView.Get(), Bits);
    Bytes = AcReadBuffer(Check, UintBuffer.Get());
    Check.Report("uint_bits", "%08X %08X %08X %08X", AcReadUint(Bytes, 0), AcReadUint(Bytes, 1), AcReadUint(Bytes, 2),
                 AcReadUint(Bytes, 3));

    /* A unorm texture converts the float clear like a shader write would */
    Check.Context->ClearUnorderedAccessViewFloat(TextureView.Get(), Half);
    Texture->GetDesc(&StagingDesc);
    StagingDesc.Usage = D3D11_USAGE_STAGING;
    StagingDesc.BindFlags = 0;
    StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (!Check.Expect(SUCCEEDED(Check.Device->CreateTexture2D(&StagingDesc, nullptr, &Staging)), "staging texture"))
        return;

    Check.Context->CopyResource(Staging.Get(), Texture.Get());
    if (Check.Expect(SUCCEEDED(Check.Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped)), "Map texture"))
    {
        Check.Report("unorm_texel", "%08X", *static_cast<UINT*>(Mapped.pData));
        Check.Context->Unmap(Staging.Get(), 0);
    }
}

/* uavbinding: compute and output merger UAV slots, and how the output merger setters share them */

static
VOID
AcCheckUavBinding(
    _In_ CheckContext& Check)
{
    ComPtr<ID3D11Texture2D> Target = AcCreateTexture(Check, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11Buffer> First = AcCreateBuffer(Check, D3D11_BIND_UNORDERED_ACCESS, 256);
    ComPtr<ID3D11Buffer> Second = AcCreateBuffer(Check, D3D11_BIND_UNORDERED_ACCESS, 256);
    D3D11_UNORDERED_ACCESS_VIEW_DESC Desc = {};
    ComPtr<ID3D11RenderTargetView> TargetView;
    ComPtr<ID3D11UnorderedAccessView> A;
    ComPtr<ID3D11UnorderedAccessView> B;
    ID3D11UnorderedAccessView* Got[4] = {};
    ID3D11RenderTargetView* GotTarget = nullptr;
    ID3D11RenderTargetView* GotTargets[2] = {};
    ID3D11RenderTargetView* Targets[2];
    ComPtr<ID3D11Texture2D> SecondTarget = AcCreateTexture(Check, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11RenderTargetView> SecondView;

    Desc.Format = DXGI_FORMAT_R32_UINT;
    Desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    Desc.Buffer.NumElements = 64;
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(First.Get(), &Desc, &A)), "UAV A");
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(Second.Get(), &Desc, &B)), "UAV B");
    Check.Expect(SUCCEEDED(Check.Device->CreateRenderTargetView(Target.Get(), nullptr, &TargetView)), "RTV");
    Check.Expect(SUCCEEDED(Check.Device->CreateRenderTargetView(SecondTarget.Get(), nullptr, &SecondView)), "RTV 2");
    if (A == nullptr || B == nullptr || TargetView == nullptr || SecondView == nullptr)
        return;

    /* Compute slots, including the last one 11.1 has and one past it */
    Check.Context->CSSetUnorderedAccessViews(2, 1, A.GetAddressOf(), nullptr);
    Check.Context->CSSetUnorderedAccessViews(63, 1, B.GetAddressOf(), nullptr);
    Check.Context->CSSetUnorderedAccessViews(64, 1, A.GetAddressOf(), nullptr);
    Check.Context->CSGetUnorderedAccessViews(1, 2, Got);
    AcSlot(Check, "cs_slot1", A.Get(), Got[0]);
    AcSlot(Check, "cs_slot2", A.Get(), Got[1]);
    Check.Context->CSGetUnorderedAccessViews(63, 1, Got);
    AcSlot(Check, "cs_slot63", B.Get(), Got[0]);

    /* A target in slot 0 and a UAV in slot 1 */
    Check.Context->OMSetRenderTargetsAndUnorderedAccessViews(1, TargetView.GetAddressOf(), nullptr, 1, 1,
                                                             A.GetAddressOf(), nullptr);
    Check.Context->OMGetRenderTargetsAndUnorderedAccessViews(1, &GotTarget, nullptr, 0, 3, Got);
    AcSlot(Check, "om_target", TargetView.Get(), GotTarget);
    AcSlot(Check, "om_uav0", A.Get(), Got[0]);
    AcSlot(Check, "om_uav1", A.Get(), Got[1]);
    AcSlot(Check, "om_uav2", A.Get(), Got[2]);

    /* Keep the targets, move the UAVs up a slot */
    Check.Context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr,
                                                             nullptr, 2, 1, B.GetAddressOf(), nullptr);
    Check.Context->OMGetRenderTargetsAndUnorderedAccessViews(1, &GotTarget, nullptr, 1, 2, Got);
    AcSlot(Check, "keep_target", TargetView.Get(), GotTarget);
    AcSlot(Check, "keep_uav1", A.Get(), Got[0]);
    AcSlot(Check, "keep_uav2", B.Get(), Got[1]);

    /* Keep the UAVs, change the targets */
    Check.Context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
                                                             D3D11_KEEP_UNORDERED_ACCESS_VIEWS, nullptr, nullptr);
    Check.Context->OMGetRenderTargetsAndUnorderedAccessViews(1, &GotTarget, nullptr, 2, 1, Got);
    AcSlot(Check, "keepuav_target", TargetView.Get(), GotTarget);
    AcSlot(Check, "keepuav_uav2", B.Get(), Got[0]);

    /* UAVs starting below the target count */
    Targets[0] = TargetView.Get();
    Targets[1] = SecondView.Get();
    Check.Context->OMSetRenderTargetsAndUnorderedAccessViews(2, Targets, nullptr, 1, 1, A.GetAddressOf(), nullptr);
    Check.Context->OMGetRenderTargetsAndUnorderedAccessViews(2, GotTargets, nullptr, 1, 2, Got);
    AcSlot(Check, "overlap_target0", TargetView.Get(), GotTargets[0]);
    AcSlot(Check, "overlap_target1", SecondView.Get(), GotTargets[1]);
    AcSlot(Check, "overlap_uav1", A.Get(), Got[0]);
    AcSlot(Check, "overlap_uav2", B.Get(), Got[1]);

    /* Reset to one target and the UAV in slot 2 */
    Check.Context->OMSetRenderTargetsAndUnorderedAccessViews(1, TargetView.GetAddressOf(), nullptr, 2, 1,
                                                             B.GetAddressOf(), nullptr);

    /* Plain OMSetRenderTargets and what happens to the UAVs */
    Check.Context->OMSetRenderTargets(1, TargetView.GetAddressOf(), nullptr);
    Check.Context->OMGetRenderTargetsAndUnorderedAccessViews(1, &GotTarget, nullptr, 2, 1, Got);
    AcSlot(Check, "plain_target", TargetView.Get(), GotTarget);
    AcSlot(Check, "plain_uav2", B.Get(), Got[0]);
}

/* hazards: what wins when a resource is bound for reading and writing at once */

static
VOID
AcCheckHazards(
    _In_ CheckContext& Check)
{
    ComPtr<ID3D11Texture2D> Texture = AcCreateTexture(Check, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
                                                                 D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11Buffer> Stream = AcCreateBuffer(Check, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT, 256);
    ComPtr<ID3D11Buffer> Writable = AcCreateBuffer(Check, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS, 256);
    D3D11_UNORDERED_ACCESS_VIEW_DESC BufferDesc = {};
    ComPtr<ID3D11ShaderResourceView> Read;
    ComPtr<ID3D11RenderTargetView> Write;
    ComPtr<ID3D11UnorderedAccessView> Access;
    ComPtr<ID3D11UnorderedAccessView> BufferAccess;
    ID3D11UnorderedAccessView* NoAccess = nullptr;
    ID3D11ShaderResourceView* GotRead = nullptr;
    ID3D11RenderTargetView* GotWrite = nullptr;
    ID3D11UnorderedAccessView* GotAccess = nullptr;
    ID3D11Buffer* GotBuffer = nullptr;
    UINT Stride = 16;
    UINT Offset = 0;

    BufferDesc.Format = DXGI_FORMAT_R32_UINT;
    BufferDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    BufferDesc.Buffer.NumElements = 64;
    Check.Expect(SUCCEEDED(Check.Device->CreateShaderResourceView(Texture.Get(), nullptr, &Read)), "SRV");
    Check.Expect(SUCCEEDED(Check.Device->CreateRenderTargetView(Texture.Get(), nullptr, &Write)), "RTV");
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(Texture.Get(), nullptr, &Access)), "UAV");
    Check.Expect(SUCCEEDED(Check.Device->CreateUnorderedAccessView(Writable.Get(), &BufferDesc, &BufferAccess)),
                 "buffer UAV");
    if (Read == nullptr || Write == nullptr || Access == nullptr || BufferAccess == nullptr)
        return;

    /* A compute UAV unbinds reads in graphics stages and in compute */
    Check.Context->PSSetShaderResources(0, 1, Read.GetAddressOf());
    Check.Context->CSSetShaderResources(0, 1, Read.GetAddressOf());
    Check.Context->CSSetUnorderedAccessViews(0, 1, Access.GetAddressOf(), nullptr);
    Check.Context->PSGetShaderResources(0, 1, &GotRead);
    AcSlot(Check, "csuav_unbinds_ps_srv", Read.Get(), GotRead);
    Check.Context->CSGetShaderResources(0, 1, &GotRead);
    AcSlot(Check, "csuav_unbinds_cs_srv", Read.Get(), GotRead);

    /* Reading what is being written binds nothing */
    Check.Context->PSSetShaderResources(0, 1, Read.GetAddressOf());
    Check.Context->PSGetShaderResources(0, 1, &GotRead);
    AcSlot(Check, "srv_while_csuav", Read.Get(), GotRead);

    /* Output against output: the newer binding wins */
    Check.Context->OMSetRenderTargets(1, Write.GetAddressOf(), nullptr);
    Check.Context->CSGetUnorderedAccessViews(0, 1, &GotAccess);
    AcSlot(Check, "rtv_unbinds_csuav", Access.Get(), GotAccess);
    Check.Context->CSSetUnorderedAccessViews(0, 1, Access.GetAddressOf(), nullptr);
    Check.Context->OMGetRenderTargets(1, &GotWrite, nullptr);
    AcSlot(Check, "csuav_unbinds_rtv", Write.Get(), GotWrite);
    Check.Context->CSSetUnorderedAccessViews(0, 1, &NoAccess, nullptr);

    /* Vertex buffers lose to stream output and to UAVs, both ways round */
    Check.Context->IASetVertexBuffers(0, 1, Stream.GetAddressOf(), &Stride, &Offset);
    Check.Context->SOSetTargets(1, Stream.GetAddressOf(), &Offset);
    Check.Context->IAGetVertexBuffers(0, 1, &GotBuffer, nullptr, nullptr);
    AcSlot(Check, "so_unbinds_vb", Stream.Get(), GotBuffer);
    Check.Context->IASetVertexBuffers(0, 1, Stream.GetAddressOf(), &Stride, &Offset);
    Check.Context->IAGetVertexBuffers(0, 1, &GotBuffer, nullptr, nullptr);
    AcSlot(Check, "vb_while_so", Stream.Get(), GotBuffer);
    Check.Context->SOSetTargets(0, nullptr, nullptr);

    Check.Context->IASetVertexBuffers(0, 1, Writable.GetAddressOf(), &Stride, &Offset);
    Check.Context->CSSetUnorderedAccessViews(0, 1, BufferAccess.GetAddressOf(), nullptr);
    Check.Context->IAGetVertexBuffers(0, 1, &GotBuffer, nullptr, nullptr);
    AcSlot(Check, "csuav_unbinds_vb", Writable.Get(), GotBuffer);
}

/* streamout: creation rules, capture contents, appending and the getters */

static
VOID
AcCheckStreamOutput(
    _In_ CheckContext& Check)
{
    static const D3D11_SO_DECLARATION_ENTRY Full[] =
    {
        { 0, "SV_Position", 0, 0, 4, 0 },
        { 0, "COLOR", 0, 0, 4, 0 },
    };
    static const D3D11_SO_DECLARATION_ENTRY Gapped[] =
    {
        { 0, "SV_Position", 0, 0, 2, 0 },
        { 0, nullptr, 0, 0, 3, 0 },
        { 0, "COLOR", 0, 1, 3, 0 },
    };
    static const D3D11_SO_DECLARATION_ENTRY Unknown[] =
    {
        { 0, "BOGUS", 0, 0, 4, 0 },
    };
    static const float Triangle[3][6] =
    {
        {  0.0f,  0.5f, 1.0f, 0.0f, 0.0f, 1.0f },
        {  0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 1.0f },
    };
    UINT Strides[5] = { 32, 32, 32, 32, 32 };
    UINT GappedStride = 32;
    ComPtr<ID3D11GeometryShader> Capture;
    ComPtr<ID3D11GeometryShader> Other;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> Source = AcCreateBuffer(Check, D3D11_BIND_VERTEX_BUFFER, sizeof(Triangle), 0, 0, Triangle);
    ComPtr<ID3D11Buffer> Captured = AcCreateBuffer(Check, D3D11_BIND_STREAM_OUTPUT, 6 * 32);
    ID3D11Buffer* GotTargets[4] = {};
    std::vector<BYTE> Bytes;
    UINT Stride = sizeof(Triangle[0]);
    UINT Offset = 0;
    UINT Append = (UINT)-1;
    FLOAT Value;
    HRESULT hr;

    hr = Check.Device->CreateGeometryShaderWithStreamOutput(g_SimpleVS, sizeof(g_SimpleVS), Full, 2, Strides, 1,
                                                            D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &Capture);
    Check.Report("vs_bytecode", "0x%08lX", hr);
    hr = Check.Device->CreateGeometryShaderWithStreamOutput(g_PassGS, sizeof(g_PassGS), Full, 2, Strides, 1, 0,
                                                            nullptr, &Other);
    Check.Report("gs_bytecode", "0x%08lX", hr);
    Other.Reset();
    hr = Check.Device->CreateGeometryShaderWithStreamOutput(g_SimpleVS, sizeof(g_SimpleVS), Gapped, 3, &GappedStride,
                                                            1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &Other);
    Check.Report("gapped", "0x%08lX", hr);
    Other.Reset();
    hr = Check.Device->CreateGeometryShaderWithStreamOutput(g_SimpleVS, sizeof(g_SimpleVS), Full, 0, nullptr, 0,
                                                            D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &Other);
    Check.Report("no_entries", "0x%08lX", hr);
    Other.Reset();
    hr = Check.Device->CreateGeometryShaderWithStreamOutput(g_SimpleVS, sizeof(g_SimpleVS), Unknown, 1, Strides, 1,
                                                            D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &Other);
    Check.Report("unknown_semantic", "0x%08lX", hr);
    Other.Reset();
    hr = Check.Device->CreateGeometryShaderWithStreamOutput(g_SimpleVS, sizeof(g_SimpleVS), Full, 2, Strides, 1, 5,
                                                            nullptr, &Other);
    Check.Report("bad_rasterized", "0x%08lX", hr);
    Other.Reset();
    hr = Check.Device->CreateGeometryShaderWithStreamOutput(g_SimpleVS, sizeof(g_SimpleVS), Full, 2, Strides, 5,
                                                            D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &Other);
    Check.Report("too_many_strides", "0x%08lX", hr);
    Other.Reset();
    hr = Check.Device->CreateGeometryShaderWithStreamOutput(g_SimplePS, sizeof(g_SimplePS), Full, 2, Strides, 1,
                                                            D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &Other);
    Check.Report("ps_bytecode", "0x%08lX", hr);
    Other.Reset();

    Check.Expect(SUCCEEDED(Check.Device->CreateVertexShader(g_SimpleVS, sizeof(g_SimpleVS), nullptr, &VertexShader)),
                 "CreateVertexShader");
    Check.Expect(SUCCEEDED(Check.Device->CreateInputLayout(AcSimpleLayout, _countof(AcSimpleLayout), g_SimpleVS,
                                                           sizeof(g_SimpleVS), &Layout)),
                 "CreateInputLayout");
    if (Capture == nullptr || VertexShader == nullptr || Layout == nullptr)
        return;

    Check.Context->IASetInputLayout(Layout.Get());
    Check.Context->IASetVertexBuffers(0, 1, Source.GetAddressOf(), &Stride, &Offset);
    Check.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Check.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
    Check.Context->GSSetShader(Capture.Get(), nullptr, 0);

    /* The first draw writes from offset zero, the second appends after it */
    Check.Context->SOSetTargets(1, Captured.GetAddressOf(), &Offset);
    Check.Context->Draw(3, 0);
    Check.Context->SOSetTargets(1, Captured.GetAddressOf(), &Append);
    Check.Context->Draw(3, 0);

    Check.Context->SOGetTargets(4, GotTargets);
    AcSlot(Check, "target0", Captured.Get(), GotTargets[0]);
    AcSlot(Check, "target1", Captured.Get(), GotTargets[1]);

    Bytes = AcReadBuffer(Check, Captured.Get());
    if (Bytes.size() < 6 * 32)
        return;

    memcpy(&Value, &Bytes[1 * 32 + 0], sizeof(Value));
    Check.Report("vertex1_x", "%.3f", Value);
    memcpy(&Value, &Bytes[2 * 32 + 16 + 8], sizeof(Value));
    Check.Report("vertex2_blue", "%.3f", Value);
    memcpy(&Value, &Bytes[3 * 32 + 4], sizeof(Value));
    Check.Report("appended_y", "%.3f", Value);
}

/* shaders: each stage takes only its own bytecode, and the getters return what was set */

static
VOID
AcCheckShaders(
    _In_ CheckContext& Check)
{
    ComPtr<ID3D11GeometryShader> Geometry;
    ComPtr<ID3D11HullShader> Hull;
    ComPtr<ID3D11DomainShader> Domain;
    ComPtr<ID3D11ComputeShader> Compute;
    ComPtr<ID3D11ComputeShader> Wrong;
    ComPtr<ID3D11HullShader> WrongHull;
    ID3D11GeometryShader* GotGeometry = nullptr;
    ID3D11HullShader* GotHull = nullptr;
    ID3D11DomainShader* GotDomain = nullptr;
    ID3D11ComputeShader* GotCompute = nullptr;
    HRESULT hr;

    hr = Check.Device->CreateGeometryShader(g_PassGS, sizeof(g_PassGS), nullptr, &Geometry);
    Check.Report("geometry", "0x%08lX", hr);
    hr = Check.Device->CreateHullShader(g_PassHS, sizeof(g_PassHS), nullptr, &Hull);
    Check.Report("hull", "0x%08lX", hr);
    hr = Check.Device->CreateDomainShader(g_PassDS, sizeof(g_PassDS), nullptr, &Domain);
    Check.Report("domain", "0x%08lX", hr);
    hr = Check.Device->CreateComputeShader(g_SquaresCS, sizeof(g_SquaresCS), nullptr, &Compute);
    Check.Report("compute", "0x%08lX", hr);
    hr = Check.Device->CreateComputeShader(g_SimpleVS, sizeof(g_SimpleVS), nullptr, &Wrong);
    Check.Report("compute_from_vs", "0x%08lX", hr);
    hr = Check.Device->CreateHullShader(g_PassDS, sizeof(g_PassDS), nullptr, &WrongHull);
    Check.Report("hull_from_ds", "0x%08lX", hr);
    hr = Check.Device->CreateGeometryShader(g_PassGS, sizeof(g_PassGS), nullptr, nullptr);
    Check.Report("geometry_null_out", "0x%08lX", hr);

    Check.Context->GSSetShader(Geometry.Get(), nullptr, 0);
    Check.Context->HSSetShader(Hull.Get(), nullptr, 0);
    Check.Context->DSSetShader(Domain.Get(), nullptr, 0);
    Check.Context->CSSetShader(Compute.Get(), nullptr, 0);
    Check.Context->GSGetShader(&GotGeometry, nullptr, nullptr);
    Check.Context->HSGetShader(&GotHull, nullptr, nullptr);
    Check.Context->DSGetShader(&GotDomain, nullptr, nullptr);
    Check.Context->CSGetShader(&GotCompute, nullptr, nullptr);
    AcSlot(Check, "gs_bound", Geometry.Get(), GotGeometry);
    AcSlot(Check, "hs_bound", Hull.Get(), GotHull);
    AcSlot(Check, "ds_bound", Domain.Get(), GotDomain);
    AcSlot(Check, "cs_bound", Compute.Get(), GotCompute);

    Check.Context->ClearState();
    Check.Context->CSGetShader(&GotCompute, nullptr, nullptr);
    AcSlot(Check, "cs_after_clear", Compute.Get(), GotCompute);
}

static const CheckInfo ComputeChecks[] =
{
    { "compute", AcCheckCompute },
    { "append", AcCheckAppend },
    { "uavclear", AcCheckUavClear },
    { "uavbinding", AcCheckUavBinding },
    { "hazards", AcCheckHazards },
    { "streamout", AcCheckStreamOutput },
    { "shaders", AcCheckShaders },
};

const CheckInfo*
AcComputeChecks(
    _Out_ UINT* Count)
{
    *Count = _countof(ComputeChecks);
    return ComputeChecks;
}
