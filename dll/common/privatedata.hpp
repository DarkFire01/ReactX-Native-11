/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Private data storage shared by the device and its children
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <vector>

/* Private data attached through SetPrivateData and SetPrivateDataInterface */
class RxPrivateDataStore
{
public:
    ~RxPrivateDataStore()
    {
        for (Entry& Item : Entries)
        {
            if (Item.Interface != nullptr)
                Item.Interface->Release();
        }
    }

    HRESULT
    Get(
        _In_ REFGUID Guid,
        _Inout_ UINT* DataSize,
        _Out_writes_bytes_opt_(*DataSize) VOID* Data)
    {
        UINT Needed;

        if (DataSize == nullptr)
            return E_INVALIDARG;

        for (Entry& Item : Entries)
        {
            if (Item.Guid != Guid)
                continue;

            Needed = Item.Interface ? (UINT)sizeof(IUnknown*) : (UINT)Item.Bytes.size();
            if (Data == nullptr)
            {
                *DataSize = Needed;
                return S_OK;
            }
            if (*DataSize < Needed)
            {
                *DataSize = Needed;
                return DXGI_ERROR_MORE_DATA;
            }

            *DataSize = Needed;
            if (Item.Interface != nullptr)
            {
                Item.Interface->AddRef();
                memcpy(Data, &Item.Interface, sizeof(IUnknown*));
            }
            else if (Needed != 0)
            {
                memcpy(Data, Item.Bytes.data(), Needed);
            }
            return S_OK;
        }

        *DataSize = 0;
        return DXGI_ERROR_NOT_FOUND;
    }

    HRESULT
    Set(
        _In_ REFGUID Guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const VOID* Data,
        _In_opt_ const IUnknown* Interface)
    {
        Entry* Existing = nullptr;
        IUnknown* Held = const_cast<IUnknown*>(Interface);

        for (Entry& Item : Entries)
        {
            if (Item.Guid == Guid)
                Existing = &Item;
        }

        if (Existing != nullptr)
        {
            if (Existing->Interface != nullptr)
                Existing->Interface->Release();
            Existing->Interface = nullptr;
            Existing->Bytes.clear();
        }

        /* Nothing to store means remove the entry */
        if (Held == nullptr && (Data == nullptr || DataSize == 0))
        {
            if (Existing != nullptr)
                Entries.erase(Entries.begin() + (Existing - Entries.data()));
            return S_OK;
        }

        if (Existing == nullptr)
        {
            Entries.push_back({ Guid, {}, nullptr });
            Existing = &Entries.back();
        }

        if (Held != nullptr)
        {
            Held->AddRef();
            Existing->Interface = Held;
        }
        else
        {
            Existing->Bytes.assign((const UINT8*)Data, (const UINT8*)Data + DataSize);
        }
        return S_OK;
    }

private:
    struct Entry
    {
        GUID Guid;
        std::vector<UINT8> Bytes;
        IUnknown* Interface;
    };

    std::vector<Entry> Entries;
};
