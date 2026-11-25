/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Reading a Direct3D 12 swapchain buffer back, for GDI presents and frame dumps
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "swapchain.hpp"

#include <d3d12.h>

/* A transition of the whole buffer between two states */
static
D3D12_RESOURCE_BARRIER
RxTransition(
    _In_ ID3D12Resource* Resource,
    _In_ D3D12_RESOURCE_STATES Before,
    _In_ D3D12_RESOURCE_STATES After)
{
    D3D12_RESOURCE_BARRIER Barrier = {};

    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Resource;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = Before;
    Barrier.Transition.StateAfter = After;
    return Barrier;
}

HRESULT
RxReadBuffer12(
    _In_ IUnknown* Queue,
    _In_ IUnknown* Buffer,
    _Out_ std::vector<UINT>* Pixels,
    _Out_ UINT* Width,
    _Out_ UINT* Height)
{
    ID3D12CommandQueue* CommandQueue = nullptr;
    ID3D12Resource* Source = nullptr;
    ID3D12Device* Device = nullptr;
    ID3D12CommandAllocator* Allocator = nullptr;
    ID3D12GraphicsCommandList* List = nullptr;
    ID3D12Resource* Readback = nullptr;
    ID3D12Fence* Fence = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint = {};
    D3D12_TEXTURE_COPY_LOCATION From = {};
    D3D12_TEXTURE_COPY_LOCATION To = {};
    D3D12_HEAP_PROPERTIES Heap = {};
    D3D12_RESOURCE_DESC CopyDesc = {};
    D3D12_RESOURCE_DESC SourceDesc;
    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    D3D12_RESOURCE_BARRIER Barrier;
    D3D12_RANGE Range = {};
    D3D12_RANGE Written = {};
    UINT64 TotalSize = 0;
    BYTE* Mapped = nullptr;
    HRESULT hr;
    UINT Y;

    Pixels->clear();
    *Width = 0;
    *Height = 0;

    hr = Queue->QueryInterface(IID_PPV_ARGS(&CommandQueue));
    if (SUCCEEDED(hr))
        hr = Buffer->QueryInterface(IID_PPV_ARGS(&Source));
    if (SUCCEEDED(hr))
        hr = Source->GetDevice(IID_PPV_ARGS(&Device));
    if (FAILED(hr))
        goto Done;

    SourceDesc = Source->GetDesc();
    QueueDesc = CommandQueue->GetDesc();
    Device->GetCopyableFootprints(&SourceDesc, 0, 1, 0, &Footprint, nullptr, nullptr, &TotalSize);

    Heap.Type = D3D12_HEAP_TYPE_READBACK;
    CopyDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    CopyDesc.Width = TotalSize;
    CopyDesc.Height = 1;
    CopyDesc.DepthOrArraySize = 1;
    CopyDesc.MipLevels = 1;
    CopyDesc.SampleDesc.Count = 1;
    CopyDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &CopyDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                         nullptr, IID_PPV_ARGS(&Readback));
    if (SUCCEEDED(hr))
        hr = Device->CreateCommandAllocator(QueueDesc.Type, IID_PPV_ARGS(&Allocator));
    if (SUCCEEDED(hr))
        hr = Device->CreateCommandList(0, QueueDesc.Type, Allocator, nullptr, IID_PPV_ARGS(&List));
    if (SUCCEEDED(hr))
        hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
    if (FAILED(hr))
        goto Done;

    /* Swapchain buffers rest in the present state between frames */
    From.pResource = Source;
    From.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    To.pResource = Readback;
    To.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    To.PlacedFootprint = Footprint;

    Barrier = RxTransition(Source, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    List->ResourceBarrier(1, &Barrier);
    List->CopyTextureRegion(&To, 0, 0, 0, &From, nullptr);
    Barrier = RxTransition(Source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    List->ResourceBarrier(1, &Barrier);
    hr = List->Close();

    /* The copy is ordered after the frame already on the queue */
    if (SUCCEEDED(hr))
    {
        ID3D12CommandList* Lists[] = { List };

        CommandQueue->ExecuteCommandLists(_countof(Lists), Lists);
        hr = CommandQueue->Signal(Fence, 1);
    }
    if (SUCCEEDED(hr))
        hr = Fence->SetEventOnCompletion(1, NULL);

    Range.End = (SIZE_T)TotalSize;
    if (SUCCEEDED(hr))
        hr = Readback->Map(0, &Range, reinterpret_cast<void**>(&Mapped));
    if (FAILED(hr))
        goto Done;

    Pixels->resize((SIZE_T)SourceDesc.Width * SourceDesc.Height);
    for (Y = 0; Y < SourceDesc.Height; Y++)
    {
        if (!RxConvertRowToBgra(SourceDesc.Format, Mapped + Footprint.Offset + (SIZE_T)Y * Footprint.Footprint.RowPitch,
                                &(*Pixels)[(SIZE_T)Y * SourceDesc.Width], (UINT)SourceDesc.Width))
        {
            hr = DXGI_ERROR_UNSUPPORTED;
            break;
        }
    }
    Readback->Unmap(0, &Written);

    if (SUCCEEDED(hr))
    {
        *Width = (UINT)SourceDesc.Width;
        *Height = SourceDesc.Height;
    }
    else
    {
        Pixels->clear();
    }

Done:
    if (Fence != nullptr)
        Fence->Release();
    if (List != nullptr)
        List->Release();
    if (Allocator != nullptr)
        Allocator->Release();
    if (Readback != nullptr)
        Readback->Release();
    if (Device != nullptr)
        Device->Release();
    if (Source != nullptr)
        Source->Release();
    if (CommandQueue != nullptr)
        CommandQueue->Release();
    return hr;
}
