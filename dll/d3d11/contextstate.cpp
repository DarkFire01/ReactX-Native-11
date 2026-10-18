/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device context state objects and swapping them on the immediate context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "queries.hpp"
#include "shaders.hpp"
#include "states.hpp"
#include "views.hpp"

/* A whole pipeline parked while another state object is active on the context */
class RxContextState final : public RxDeviceChild<ID3DDeviceContextState>
{
public:
    static constexpr GUID PrivateId = { 0x6d1f84b2, 0x9ac3, 0x4e57, { 0x8b, 0x20, 0x4f, 0xe1, 0x73, 0x5a, 0xc6, 0x9d } };

    explicit RxContextState(
        _In_ RxDevice& Device)
        : RxDeviceChild<ID3DDeviceContextState>(Device)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == PrivateId)
        {
            *ppvObject = this;
            return S_OK;
        }

        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
            riid == __uuidof(ID3DDeviceContextState))
        {
            *ppvObject = static_cast<ID3DDeviceContextState*>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    /* The bindings while this state is not the active one */
    RxPipelineState Saved;

    /* Never activated yet, so it starts from the default pipeline */
    BOOLEAN IsFresh = TRUE;

private:
    ~RxContextState() override = default;
};

/* Interfaces a state object may stand for; D3D10 devices share the same pipeline here */
static
BOOLEAN
RxIsEmulatableInterface(
    _In_ REFIID Interface)
{
    return Interface == __uuidof(ID3D11Device) || Interface == __uuidof(ID3D11Device1) ||
           Interface == __uuidof(ID3D11Device2) || Interface == __uuidof(ID3D11Device3) ||
           Interface == __uuidof(ID3D11Device4) || Interface == __uuidof(ID3D11Device5) ||
           Interface == __uuidof(ID3D10Device) || Interface == __uuidof(ID3D10Device1);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateDeviceContextState(
    UINT Flags,
    _In_reads_(FeatureLevels) const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    REFIID EmulatedInterface,
    _Out_opt_ D3D_FEATURE_LEVEL* pChosenFeatureLevel,
    _Out_opt_ ID3DDeviceContextState** ppContextState)
{
    RxContextState* State;
    D3D_FEATURE_LEVEL Chosen = (D3D_FEATURE_LEVEL)0;
    UINT Index;

    UNREFERENCED_PARAMETER(SDKVersion);

    if (ppContextState != nullptr)
        *ppContextState = nullptr;
    if (pChosenFeatureLevel != nullptr)
        *pChosenFeatureLevel = (D3D_FEATURE_LEVEL)0;

    if ((Flags & ~D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED) != 0 || pFeatureLevels == nullptr ||
        FeatureLevels == 0 || !RxIsEmulatableInterface(EmulatedInterface))
    {
        return E_INVALIDARG;
    }

    /* The first requested level the device reaches */
    for (Index = 0; Index < FeatureLevels && Chosen == 0; Index++)
    {
        if (pFeatureLevels[Index] <= GetFeatureLevel())
            Chosen = pFeatureLevels[Index];
    }

    if (Chosen == 0)
        return E_INVALIDARG;

    if (pChosenFeatureLevel != nullptr)
        *pChosenFeatureLevel = Chosen;
    if (ppContextState == nullptr)
        return S_FALSE;

    State = new (std::nothrow) RxContextState(*this);
    if (State == nullptr)
        return E_OUTOFMEMORY;

    *ppContextState = State;
    return S_OK;
}

/* Hands every binding to the driver again, after the pipeline was replaced wholesale */
VOID
RxImmediateContext::ResendPipeline()
{
    UINT Stage;

    for (Stage = 0; Stage < RxStageCount; Stage++)
    {
        SendShader((RxStage)Stage);
        SendConstantBuffers((RxStage)Stage, 0, UINT_MAX);
        SendShaderResources((RxStage)Stage, 0, UINT_MAX);
        SendSamplers((RxStage)Stage, 0, UINT_MAX);
    }

    SendInputLayout();
    SendVertexBuffers(0, UINT_MAX);
    SendIndexBuffer();
    SendTopology();
    SendRenderTargets(0);
    SendBlendState();
    SendDepthStencilState();
    SendRasterizerState();
    SendViewports(0);
    SendScissorRects(0);
    SendComputeUavs(0, UINT_MAX);
    SendStreamTargets(0);

    Device.GetDeviceFuncs().pfnSetPredication(Device.GetDriverDevice(),
                                              Pipeline.Predicate ? Pipeline.Predicate->GetHandle() : D3D10DDI_HQUERY{},
                                              Pipeline.PredicateValue);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::SwapDeviceContextState(
    _In_ ID3DDeviceContextState* pState,
    _Outptr_opt_ ID3DDeviceContextState** ppPreviousState)
{
    RxContextState* Next = RxUnwrap<RxContextState>(pState);
    RxContextState* Previous = static_cast<RxContextState*>(ActiveState);
    RxPipelineState Leaving;

    if (ppPreviousState != nullptr)
        *ppPreviousState = nullptr;

    /* Before the first swap the context runs on a state nobody created; it gets an object now */
    if (Previous == nullptr)
    {
        Previous = new (std::nothrow) RxContextState(Device);
        if (Previous == nullptr)
            return;
        Previous->IsFresh = FALSE;
        Previous->AddInternalRef();
        Previous->Release();
        ActiveState = Previous;
    }

    if (ppPreviousState != nullptr)
    {
        Previous->AddRef();
        *ppPreviousState = Previous;
    }

    if (Next == nullptr || Next == Previous)
        return;

    /* Park the current bindings, unbind everything from the driver, then bring the next state in */
    Leaving = Pipeline;
    ClearState();
    Previous->Saved = Leaving;
    Leaving = {};

    if (Next->IsFresh)
    {
        Next->IsFresh = FALSE;
        LoadPipelineDefaults();
    }
    else
    {
        Pipeline = Next->Saved;
        Next->Saved = {};
        ResendPipeline();
    }

    Next->AddInternalRef();
    ActiveState = Next;
    Previous->ReleaseInternalRef();
}

VOID
RxImmediateContext::ReleaseContextState()
{
    RxContextState* Active = static_cast<RxContextState*>(ActiveState);

    ActiveState = nullptr;
    if (Active != nullptr)
        Active->ReleaseInternalRef();
}

/* Private data on the context */

HRESULT
STDMETHODCALLTYPE
RxImmediateContext::GetPrivateData(
    _In_ REFGUID guid,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_opt_(*pDataSize) void* pData)
{
    return PrivateData.Get(guid, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxImmediateContext::SetPrivateData(
    _In_ REFGUID guid,
    _In_ UINT DataSize,
    _In_reads_bytes_opt_(DataSize) const void* pData)
{
    return PrivateData.Set(guid, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxImmediateContext::SetPrivateDataInterface(
    _In_ REFGUID guid,
    _In_opt_ const IUnknown* pData)
{
    return PrivateData.Set(guid, 0, nullptr, pData);
}
