/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Windows side of the layer over the open source device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "pch.hpp"
#include "trace.hpp"

/* A surface handed to the D3D12 runtime's present path */
struct ON12_PRESENT_SURFACE
{
    ID3D12Resource* Resource;
    UINT Subresource;
};

/* What the D3D12 runtime fills while recording a present, then reads back when it submits it */
struct ON12_PRESENT_RECORD
{
    BYTE Runtime[1060];
    UINT Initialized;
    BYTE Reserved[12];
    UINT VidPnSourceId;
};
static_assert(sizeof(ON12_PRESENT_RECORD) == 1080, "D3D12 expects a 1080 byte present record");

/* Present model the command list records for a flip or a blt */
#define ON12_PRESENT_MODEL_FLIP 2
#define ON12_PRESENT_MODEL_BLT 4

/* Present recording on the direct command list, exposed by D3D12 */
struct DECLSPEC_UUID("b0b9e70e-59ce-4415-8116-c8c826b1e599") DECLSPEC_NOVTABLE
IOn12PresentCommandList : public IUnknown
{
    STDMETHOD_(void, Unused0)() = 0;
    STDMETHOD_(void, RecordPresent)(
        _In_ ID3D12CommandQueue* Queue,
        _In_reads_(SurfaceCount) const ON12_PRESENT_SURFACE* Surfaces,
        _In_ UINT SurfaceCount,
        _In_opt_ ID3D12Resource* Destination,
        _In_opt_ void* Unused1,
        _In_opt_ void* Unused2,
        _In_ UINT PresentModel,
        _In_ UINT FlipInterval,
        _Out_ BOOL* CommandsAdded,
        _Inout_ ON12_PRESENT_RECORD* Record) = 0;
};

/* Present submission on the direct queue, exposed by D3D12 */
struct DECLSPEC_UUID("c2c09af9-15ce-4c87-bb98-44323032fa7f") DECLSPEC_NOVTABLE
IOn12PresentQueue : public IUnknown
{
    STDMETHOD(SubmitPresent)(
        _In_ D3DKMT_PRESENT* KmtPresent,
        _In_ ON12_PRESENT_RECORD* Record) = 0;
};

/* The present work of the immediate context, with its own frame latency tracking */
class On12PresentContext
{
public:
    explicit On12PresentContext(
        _In_ D3D12TranslationLayer::ImmediateContext& Context);

    VOID
    Present(
        _In_reads_(SurfaceCount) const D3D12TranslationLayer::PresentSurface* Surfaces,
        _In_ UINT SurfaceCount,
        _In_opt_ D3D12TranslationLayer::Resource* Destination,
        _In_ UINT FlipInterval,
        _In_ UINT PresentModel,
        _In_ UINT VidPnSourceId,
        _In_ D3DKMT_PRESENT* KmtPresent,
        _In_ bool DoNotSequence);

    D3D12TranslationLayer::MaxFrameLatencyHelper Latency;

private:
    D3D12TranslationLayer::ImmediateContext& m_Context;
    ON12_PRESENT_RECORD m_Record = {};
    CComPtr<IOn12PresentQueue> m_PresentQueue;
};

/* Present arguments carried through the batch, followed by the surfaces */
struct On12PresentArgs
{
    UINT SurfaceCount;
    UINT FlipInterval;
    D3D11On12::Resource* Destination;
    void* DxgiContext;
    UINT VidPnSourceId;

    explicit On12PresentArgs(
        _In_ const DXGI1_6_1_DDI_ARG_PRESENT* Args);

    static
    size_t
    GetExtensionSize(
        _In_ const DXGI1_6_1_DDI_ARG_PRESENT* Args);

    D3D12TranslationLayer::PresentSurface*
    Surfaces()
    {
        return reinterpret_cast<D3D12TranslationLayer::PresentSurface*>(this + 1);
    }

    const D3D12TranslationLayer::PresentSurface*
    Surfaces() const
    {
        return reinterpret_cast<const D3D12TranslationLayer::PresentSurface*>(this + 1);
    }
};

class On12Device;

/* Runs a batched present on the immediate context thread */
struct On12PresentExtension : D3D12TranslationLayer::BatchedExtension
{
    On12Device* const Device;

    explicit On12PresentExtension(
        _In_ On12Device* Owner)
        : Device(Owner)
    {
    }

    void
    Dispatch(
        _In_ D3D12TranslationLayer::ImmediateContext& Context,
        _In_ const void* Data,
        _In_ size_t DataSize) final;
};

/* The immediate device, presenting through D3D12 instead of the redirected DXGI path */
class On12Device : public D3D11On12::Device
{
public:
    On12Device(
        _In_ D3D11On12::Adapter* Adapter,
        _Inout_ D3D10DDIARG_CREATEDEVICE* Args);

    static
    HRESULT
    APIENTRY
    Present1(
        _In_ DXGI1_6_1_DDI_ARG_PRESENT* Args);

    static
    void
    APIENTRY
    DestroyDevice(
        _In_ D3D10DDI_HDEVICE Device);

    STDMETHOD(Present)(
        _In_ D3DKMT_PRESENT* KmtPresent) noexcept final;

    STDMETHOD_(void, SetMaximumFrameLatency)(
        _In_ UINT MaxFrameLatency) noexcept final;

    STDMETHOD_(bool, IsMaximumFrameLatencyReached)() noexcept final;

private:
    friend struct On12PresentExtension;

    const On12PresentArgs* m_PendingPresent = nullptr;
    On12PresentExtension m_PresentExtension{ this };
    On12PresentContext m_PresentContext;
};

SIZE_T
APIENTRY
On12CalcPrivateDeviceSize(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ const D3D10DDIARG_CALCPRIVATEDEVICESIZE* Args);

HRESULT
APIENTRY
On12CreateDevice(
    _In_ D3D10DDI_HADAPTER Adapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* Args);
