/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12PipelineLibrary: named pipeline states kept for later runs
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/* The header every serialized library starts with, before its entries and the driver's data */
constexpr SIZE_T RxLibraryHeaderSize = 872;

/* Blobs shorter than the fixed part of the header are not libraries at all */
constexpr SIZE_T RxLibraryMinimumBlob = 0x350;

HRESULT
RxPipelineLibrary::Create(
    _In_ RxDevice12& Device,
    _Outptr_ RxPipelineLibrary** Library)
{
    RxPipelineLibrary* Created;
    HRESULT hr;

    *Library = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxPipelineLibrary(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize();
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Library = Created;
    return S_OK;
}

HRESULT
RxPipelineLibrary::Initialize()
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 Args = {};
    HRESULT hr;

    /* A library with no pipelines hands the driver nothing to load */
    DriverLibrary.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivatePipelineLibrarySize(Device.Handle(), &Args));
    if (DriverLibrary.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreatePipelineLibrary(Device.Handle(), &Args, DriverLibrary);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreatePipelineLibrary failed 0x%08lX", hr);
        return hr == E_INVALIDARG ? D3D12_ERROR_DRIVER_VERSION_MISMATCH : hr;
    }
    IsDriverLibraryCreated = TRUE;
    return S_OK;
}

RxPipelineLibrary::~RxPipelineLibrary()
{
    if (IsDriverLibraryCreated)
        Device.Ddi().pfnDestroyPipelineLibrary(Device.Handle(), DriverLibrary);
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    /* A library is a device child but not pageable */
    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12PipelineLibrary) || riid == __uuidof(ID3D12PipelineLibrary1) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12PipelineLibrary1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

SIZE_T
STDMETHODCALLTYPE
RxPipelineLibrary::GetSerializedSize(VOID)
{
    SIZE_T Size;

    AcquireSRWLockExclusive(&Lock);
    if (SerializedSize == 0)
        SerializedSize = RxLibraryHeaderSize + Device.Ddi().pfnCalcSerializedLibrarySize(Device.Handle(), DriverLibrary);
    Size = SerializedSize;
    ReleaseSRWLockExclusive(&Lock);
    return Size;
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::StorePipeline(
    _In_opt_ LPCWSTR pName,
    _In_ ID3D12PipelineState* pPipeline)
{
    RX_UNIMPLEMENTED();
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::LoadGraphicsPipeline(
    _In_ LPCWSTR pName,
    _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RX_UNIMPLEMENTED();
    if (ppPipelineState != nullptr)
        *ppPipelineState = nullptr;
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::LoadComputePipeline(
    _In_ LPCWSTR pName,
    _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RX_UNIMPLEMENTED();
    if (ppPipelineState != nullptr)
        *ppPipelineState = nullptr;
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::Serialize(
    _Out_writes_(DataSizeInBytes) void* pData,
    SIZE_T DataSizeInBytes)
{
    RX_UNIMPLEMENTED();
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxPipelineLibrary::LoadPipeline(
    _In_ LPCWSTR pName,
    _In_ const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RX_UNIMPLEMENTED();
    if (ppPipelineState != nullptr)
        *ppPipelineState = nullptr;
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreatePipelineLibrary(
    _In_reads_(BlobLength) const void* pLibraryBlob,
    SIZE_T BlobLength,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineLibrary)
{
    RxPipelineLibrary* Library;
    HRESULT hr;

    if (ppPipelineLibrary != nullptr)
        *ppPipelineLibrary = nullptr;

    if (BlobLength != 0)
    {
        if (BlobLength < RxLibraryMinimumBlob)
            return E_INVALIDARG;

        /* Reading back a serialized library is not done yet */
        RX_TRACE(L"loading a serialized pipeline library not supported");
        return E_NOTIMPL;
    }

    if (ppPipelineLibrary == nullptr)
        return S_FALSE;

    hr = RxPipelineLibrary::Create(*this, &Library);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Library, riid, ppPipelineLibrary);
}
