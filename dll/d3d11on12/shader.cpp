/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader creation with the feature flags D3D12 expects from Direct3D 11 bytecode
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "internal.hpp"
#include <DxbcBuilder.hpp>
#include <dxbchash.hpp>

using D3D12TranslationLayer::unique_batched_ptr;

/* The feature flag that marks bytecode as coming from the Direct3D 11 runtime */
static const UINT64 On12FeatureFlagFromD3D11 = 1ull << 63;

/* Recomputes the container checksum after its contents changed */
static
VOID
On12SignContainer(
    _Inout_updates_bytes_(Size) BYTE* Container,
    _In_ UINT Size)
{
    RxDxbcHash(Container + DXBCHashStartOffset, Size - DXBCHashStartOffset,
               Container + offsetof(DXBCHeader, Hash));
}

/* Sets the Direct3D 11 feature flag, adding a feature info blob when the bytecode has none */
static
VOID
On12PatchFeatureFlags(
    _Inout_ std::unique_ptr<BYTE[]>& Code,
    _Inout_ SIZE_T& Size)
{
    std::unique_ptr<BYTE[]> Patched;
    CDXBCParser Parser;
    UINT64 Flags = On12FeatureFlagFromD3D11;
    UINT32 PatchedSize = 0;
    UINT32 Found;
    HRESULT hr = E_FAIL;

    if (Code != nullptr)
        hr = Parser.ReadDXBC(Code.get(), reinterpret_cast<const DXBCHeader*>(Code.get())->ContainerSizeInBytes);
    ThrowFailure(hr);

    Found = Parser.FindNextMatchingBlob(DXBC_ShaderFeatureInfo, 0);
    if (Found != static_cast<UINT32>(DXBC_BLOB_NOT_FOUND))
    {
        UINT64* Existing = static_cast<UINT64*>(const_cast<void*>(Parser.GetBlob(Found)));

        *Existing |= On12FeatureFlagFromD3D11;
        On12SignContainer(Code.get(), reinterpret_cast<const DXBCHeader*>(Code.get())->ContainerSizeInBytes);
        return;
    }

    {
        CDXBCBuilder Builder(false);

        for (UINT32 Index = 0; Index < Parser.GetBlobCount(); Index += 1)
        {
            ThrowFailure(Builder.AppendBlob(static_cast<DXBCFourCC>(Parser.GetBlobFourCC(Index)),
                                            Parser.GetBlobSize(Index), Parser.GetBlob(Index)));
        }
        ThrowFailure(Builder.AppendBlob(DXBC_ShaderFeatureInfo, sizeof(Flags), &Flags));

        ThrowFailure(Builder.GetFinalDXBC(nullptr, &PatchedSize));
        Patched.reset(new BYTE[PatchedSize]);
        ThrowFailure(Builder.GetFinalDXBC(Patched.get(), &PatchedSize));
    }

    On12SignContainer(Patched.get(), PatchedSize);
    Code = std::move(Patched);
    Size = PatchedSize;
}

/* The translation layer shader behind a Direct3D 11 shader, or none for a stream output only one */
template <typename TDesc>
static
unique_batched_ptr<D3D12TranslationLayer::Shader>
On12CreateUnderlyingShader(
    _In_ On12Device* Device,
    _In_opt_ const TDesc* Desc)
{
    unique_batched_ptr<D3D12TranslationLayer::Shader> Underlying(nullptr, { Device->GetBatchedContext() });
    D3D11On12::Adapter* Adapter = Device->GetAdapter();
    std::unique_ptr<BYTE[]> Code;
    SIZE_T Size;

    if (Desc == nullptr)
        return Underlying;

    Size = Desc->SizeInBytes;
    Code.reset(new BYTE[Size]);
    memcpy(Code.get(), Desc->pFunction, Size);

    if (!Adapter->m_bSupportPrepatchedShaders)
        On12PatchFeatureFlags(Code, Size);

    if (Adapter->m_ShaderModelCaps.HighestShaderModel < D3D_SHADER_MODEL_6_0)
    {
        Underlying.reset(new D3D12TranslationLayer::Shader(&Device->GetImmediateContextNoFlush(), std::move(Code),
                                                           Size));
    }
    else
    {
        D3D11On12::ConvertShaderToDxil(Device, Code, Size, Desc->pLinkage != nullptr, Underlying);
    }

    return Underlying;
}

/* Builds the Direct3D 11 shader object in the runtime's handle storage */
template <typename TIface, typename TDesc>
static
HRESULT
On12CreateShader(
    _In_ On12Device* Device,
    _In_ D3D10DDI_HSHADER Handle,
    _In_opt_ const TDesc* Desc)
{
    try
    {
        new (Handle.pDrvPrivate) D3D11On12::Shader<TIface>(Device, On12CreateUnderlyingShader(Device, Desc));
    }
    catch (_com_error& Error)
    {
        On12Trace(L"shader creation failed with 0x%08lX", Error.Error());
        return Error.Error();
    }
    catch (std::bad_alloc&)
    {
        On12Trace(L"shader creation ran out of memory");
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

STDMETHODIMP
On12Device::CreateVertexShader(
    _In_ D3D10DDI_HSHADER Shader,
    _In_ const D3D11On12::SHADER_DESC* Desc) noexcept
{
    return On12CreateShader<ID3D11VertexShader>(this, Shader, Desc);
}

STDMETHODIMP
On12Device::CreatePixelShader(
    _In_ D3D10DDI_HSHADER Shader,
    _In_ const D3D11On12::SHADER_DESC* Desc) noexcept
{
    return On12CreateShader<ID3D11PixelShader>(this, Shader, Desc);
}

STDMETHODIMP
On12Device::CreateHullShader(
    _In_ D3D10DDI_HSHADER Shader,
    _In_ const D3D11On12::SHADER_DESC* Desc) noexcept
{
    return On12CreateShader<ID3D11HullShader>(this, Shader, Desc);
}

STDMETHODIMP
On12Device::CreateDomainShader(
    _In_ D3D10DDI_HSHADER Shader,
    _In_ const D3D11On12::SHADER_DESC* Desc) noexcept
{
    return On12CreateShader<ID3D11DomainShader>(this, Shader, Desc);
}

STDMETHODIMP
On12Device::CreateComputeShader(
    _In_ D3D10DDI_HSHADER Shader,
    _In_ const D3D11On12::SHADER_DESC* Desc) noexcept
{
    return On12CreateShader<ID3D11ComputeShader>(this, Shader, Desc);
}

STDMETHODIMP
On12Device::CreateGeometryShader(
    _In_ D3D10DDI_HSHADER Shader,
    _In_ const D3D11On12::GEOMETRY_SHADER_DESC* Desc,
    _In_opt_ const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* StreamOutput) noexcept
{
    if (StreamOutput == nullptr)
        return On12CreateShader<ID3D11GeometryShader>(this, Shader, Desc);

    try
    {
        /* Stream output without a geometry shader has only the declaration */
        const D3D11On12::GEOMETRY_SHADER_DESC* Code = StreamOutput->pShaderCode != nullptr ? Desc : nullptr;

        new (Shader.pDrvPrivate) D3D11On12::StreamOutShader(this, Desc, StreamOutput,
                                                            On12CreateUnderlyingShader(this, Code));
    }
    catch (_com_error& Error)
    {
        On12Trace(L"stream output shader creation failed with 0x%08lX", Error.Error());
        return Error.Error();
    }
    catch (std::bad_alloc&)
    {
        On12Trace(L"stream output shader creation ran out of memory");
        return E_OUTOFMEMORY;
    }

    return S_OK;
}
