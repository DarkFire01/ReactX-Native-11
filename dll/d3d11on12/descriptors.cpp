/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CPU descriptor slots handed out from heaps made on demand
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "descriptors.hpp"

O12DescriptorPool::O12DescriptorPool(
    _In_ ID3D12Device* Device,
    _In_ D3D12_DESCRIPTOR_HEAP_TYPE Type,
    _In_ UINT PerHeap,
    _In_ UINT NodeMask)
    : Device(Device),
      Type(Type),
      PerHeap(PerHeap),
      NodeMask(NodeMask),
      Increment(Device->GetDescriptorHandleIncrementSize(Type))
{
    InitializeSRWLock(&Lock);
}

HRESULT
O12DescriptorPool::Allocate(
    _Out_ D3D12_CPU_DESCRIPTOR_HANDLE* Slot)
{
    D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
    HRESULT hr = S_OK;
    Heap Fresh;

    AcquireSRWLockExclusive(&Lock);

    for (Heap& Each : Heaps)
    {
        if (Each.UsedCount == PerHeap)
            continue;

        for (UINT Index = 0; Index < PerHeap; Index++)
        {
            if (Each.Used[Index])
                continue;

            Each.Used[Index] = TRUE;
            Each.UsedCount += 1;
            Slot->ptr = Each.Start + SIZE_T(Index) * Increment;
            ReleaseSRWLockExclusive(&Lock);
            return S_OK;
        }
    }

    Desc.Type = Type;
    Desc.NumDescriptors = PerHeap;
    Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    Desc.NodeMask = NodeMask;
    hr = Device->CreateDescriptorHeap(&Desc, __uuidof(ID3D12DescriptorHeap), Fresh.Object.PutVoid());
    if (SUCCEEDED(hr))
    {
        Fresh.Start = Fresh.Object->GetCPUDescriptorHandleForHeapStart().ptr;
        Fresh.Used.assign(PerHeap, FALSE);
        Fresh.Used[0] = TRUE;
        Fresh.UsedCount = 1;
        Slot->ptr = Fresh.Start;
        Heaps.push_back(std::move(Fresh));
    }

    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

VOID
O12DescriptorPool::Free(
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE Slot)
{
    AcquireSRWLockExclusive(&Lock);

    for (Heap& Each : Heaps)
    {
        if (Slot.ptr < Each.Start || Slot.ptr >= Each.Start + SIZE_T(PerHeap) * Increment)
            continue;

        Each.Used[(Slot.ptr - Each.Start) / Increment] = FALSE;
        Each.UsedCount -= 1;
        break;
    }

    ReleaseSRWLockExclusive(&Lock);
}
