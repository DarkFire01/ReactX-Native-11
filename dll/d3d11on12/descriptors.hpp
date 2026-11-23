/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CPU descriptor slots handed out from heaps made on demand
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <vector>

class O12DescriptorPool
{
public:
    O12DescriptorPool(
        _In_ ID3D12Device* Device,
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE Type,
        _In_ UINT PerHeap,
        _In_ UINT NodeMask);

    O12DescriptorPool(const O12DescriptorPool&) = delete;
    O12DescriptorPool& operator=(const O12DescriptorPool&) = delete;

    /* The lowest free slot, making a new heap when every heap is full */
    HRESULT
    Allocate(
        _Out_ D3D12_CPU_DESCRIPTOR_HANDLE* Slot);

    VOID
    Free(
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE Slot);

private:
    struct Heap
    {
        RxComPtr<ID3D12DescriptorHeap> Object;
        SIZE_T Start;
        std::vector<BOOLEAN> Used;
        UINT UsedCount;
    };

    ID3D12Device* Device;
    D3D12_DESCRIPTOR_HEAP_TYPE Type;
    UINT PerHeap;
    UINT NodeMask;
    UINT Increment;
    std::vector<Heap> Heaps;
    SRWLOCK Lock;
};
