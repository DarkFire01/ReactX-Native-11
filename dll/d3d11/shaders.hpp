/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader objects of every stage and the input layout built against a vertex shader
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "devicechild.hpp"
#include "dxbc.hpp"
#include "formats.hpp"

#include <vector>

/* How a stage's driver entry wants its signatures */
enum class RxSignatureForm
{
    InputOutput,
    Geometry,
    Tessellation,
    None
};

/* What each stage needs: its interface, its program type and the driver entry that creates it */
template <typename Interface>
struct RxShaderStage;

template <>
struct RxShaderStage<ID3D11VertexShader>
{
    static constexpr UINT ProgramType = D3D10_SB_VERTEX_SHADER;
    static constexpr RxSignatureForm Form = RxSignatureForm::InputOutput;
    static constexpr GUID PrivateId = { 0x6f1a8c34, 0x2b7e, 0x4d59, { 0x9c, 0x03, 0xe1, 0x47, 0x5a, 0x2d, 0x88, 0x16 } };
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateVertexShader;
};

template <>
struct RxShaderStage<ID3D11PixelShader>
{
    static constexpr UINT ProgramType = D3D10_SB_PIXEL_SHADER;
    static constexpr RxSignatureForm Form = RxSignatureForm::InputOutput;
    static constexpr GUID PrivateId = { 0x1d94e7b0, 0x63c5, 0x4a2f, { 0xb8, 0x7e, 0x0d, 0x9a, 0x31, 0x6c, 0x45, 0xf2 } };
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreatePixelShader;
};

template <>
struct RxShaderStage<ID3D11GeometryShader>
{
    static constexpr UINT ProgramType = D3D10_SB_GEOMETRY_SHADER;
    static constexpr RxSignatureForm Form = RxSignatureForm::Geometry;
    static constexpr GUID PrivateId = { 0x82c4e1f6, 0x5a97, 0x4c30, { 0xbd, 0x18, 0x47, 0xe9, 0x0b, 0x6a, 0xf3, 0x25 } };
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateGeometryShader;
};

template <>
struct RxShaderStage<ID3D11HullShader>
{
    static constexpr UINT ProgramType = D3D11_SB_HULL_SHADER;
    static constexpr RxSignatureForm Form = RxSignatureForm::Tessellation;
    static constexpr GUID PrivateId = { 0x0f7d93b2, 0xc148, 0x4e6a, { 0x95, 0x2d, 0x6b, 0x30, 0xe8, 0x1f, 0x74, 0xa9 } };
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateHullShader;
};

template <>
struct RxShaderStage<ID3D11DomainShader>
{
    static constexpr UINT ProgramType = D3D11_SB_DOMAIN_SHADER;
    static constexpr RxSignatureForm Form = RxSignatureForm::Tessellation;
    static constexpr GUID PrivateId = { 0xd56a28e4, 0x17bf, 0x4f81, { 0xa0, 0x6c, 0x39, 0xd2, 0x84, 0x5e, 0x1b, 0xc7 } };
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateDomainShader;
};

template <>
struct RxShaderStage<ID3D11ComputeShader>
{
    static constexpr UINT ProgramType = D3D11_SB_COMPUTE_SHADER;
    static constexpr RxSignatureForm Form = RxSignatureForm::None;
    static constexpr GUID PrivateId = { 0x49b03e7c, 0x8d26, 0x4a15, { 0xb3, 0xf1, 0x0e, 0x75, 0xc6, 0x92, 0x5d, 0x08 } };
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateComputeShader;
};

/* What CreateGeometryShaderWithStreamOutput adds on top of the bytecode */
struct RxStreamOutputDesc
{
    const D3D11_SO_DECLARATION_ENTRY* Entries;
    UINT EntryCount;
    const UINT* Strides;
    UINT StrideCount;
    UINT RasterizedStream;
};

template <typename Interface>
class RxShader final : public RxDeviceChild<Interface>
{
public:
    using Stage = RxShaderStage<Interface>;
    static constexpr GUID PrivateId = Stage::PrivateId;

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_reads_bytes_(Size) const VOID* Bytecode,
        _In_ SIZE_T Size,
        _Outptr_ RxShader** Shader);

    /* Geometry shaders only; vertex shader bytecode is taken too and streams its outputs */
    static
    HRESULT
    CreateWithStreamOutput(
        _In_ RxDevice& Device,
        _In_reads_bytes_(Size) const VOID* Bytecode,
        _In_ SIZE_T Size,
        _In_ const RxStreamOutputDesc& StreamOutput,
        _Outptr_ RxShader** Shader);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D10DDI_HSHADER
    GetHandle() const
    {
        return Handle;
    }

    const RxShaderBytecode&
    GetBytecode() const
    {
        return Bytecode;
    }

private:
    explicit RxShader(
        _In_ RxDevice& Device)
        : RxDeviceChild<Interface>(Device)
    {
    }

    ~RxShader() override;

    HRESULT
    Finish(
        _In_ HRESULT Result);

    HRESULT
    CreateDriverShader(
        _In_opt_ const RxStreamOutputDesc* StreamOutput);

    RxShaderBytecode Bytecode;

    /* Drivers may compile late and read the signatures then, so they live as long as the shader */
    std::vector<D3D11_1DDIARG_SIGNATURE_ENTRY2> Inputs;
    std::vector<D3D11_1DDIARG_SIGNATURE_ENTRY2> Outputs;
    std::vector<D3D11_1DDIARG_SIGNATURE_ENTRY2> PatchConstants;
    std::vector<D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY> StreamDecls;
    UINT StreamStrides[D3D11_SO_BUFFER_SLOT_COUNT] = {};
    D3D11_1DDIARG_STAGE_IO_SIGNATURES Signatures = {};
    D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES TessellationSignatures = {};

    RxDriverMemory Memory;
    D3D10DDI_HSHADER Handle = {};
    BOOLEAN IsCreated = FALSE;
};

using RxVertexShader = RxShader<ID3D11VertexShader>;
using RxPixelShader = RxShader<ID3D11PixelShader>;
using RxGeometryShader = RxShader<ID3D11GeometryShader>;
using RxHullShader = RxShader<ID3D11HullShader>;
using RxDomainShader = RxShader<ID3D11DomainShader>;
using RxComputeShader = RxShader<ID3D11ComputeShader>;

class RxInputLayout final : public RxDeviceChild<ID3D11InputLayout>
{
public:
    static constexpr GUID PrivateId = { 0xa3d5f218, 0x9e0c, 0x47b6, { 0x85, 0x2f, 0x6b, 0xc1, 0x0e, 0x94, 0x7a, 0x33 } };

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_reads_(ElementCount) const D3D11_INPUT_ELEMENT_DESC* Elements,
        _In_ UINT ElementCount,
        _In_reads_bytes_(Size) const VOID* Bytecode,
        _In_ SIZE_T Size,
        _Outptr_ RxInputLayout** Layout);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D10DDI_HELEMENTLAYOUT
    GetHandle() const
    {
        return Handle;
    }

private:
    explicit RxInputLayout(
        _In_ RxDevice& Device)
        : RxDeviceChild(Device)
    {
    }

    ~RxInputLayout() override;

    RxDriverMemory Memory;
    D3D10DDI_HELEMENTLAYOUT Handle = {};
    BOOLEAN IsCreated = FALSE;
};
