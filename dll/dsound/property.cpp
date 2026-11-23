/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IKsPropertySet on buffers and the table driven property sets behind it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

static
const DsPropertyItem*
DsFindProperty(
    _In_reads_(TableCount) const DsPropertyTable* Tables,
    _In_ ULONG TableCount,
    _In_ REFGUID Set,
    _In_ ULONG Id)
{
    for (ULONG Table = 0; Table < TableCount; Table++)
    {
        if (*Tables[Table].Set != Set)
            continue;
        for (ULONG Item = 0; Item < Tables[Table].Count; Item++)
        {
            if (Tables[Table].Items[Item].Id == Id)
                return &Tables[Table].Items[Item];
        }
    }
    return nullptr;
}

HRESULT
DsPropertyQuery(
    _In_reads_(TableCount) const DsPropertyTable* Tables,
    _In_ ULONG TableCount,
    _In_ const GUID* Set,
    _In_ ULONG Id,
    _Out_ ULONG* Support)
{
    const DsPropertyItem* Item;
    ULONG Bits = 0;

    if (Set == nullptr || Support == nullptr)
        return E_INVALIDARG;
    Item = DsFindProperty(Tables, TableCount, *Set, Id);
    if (Item == nullptr)
        return E_NOTIMPL;
    if (Item->Get != nullptr)
        Bits |= KSPROPERTY_SUPPORT_GET;
    if (Item->Set != nullptr)
        Bits |= KSPROPERTY_SUPPORT_SET;
    *Support = Bits;
    return S_OK;
}

/* A property found with too little room answers with the size it needs, failing only when room was offered */
HRESULT
DsPropertyGet(
    _In_reads_(TableCount) const DsPropertyTable* Tables,
    _In_ ULONG TableCount,
    _In_opt_ PVOID Owner,
    _In_ const GUID* Set,
    _In_ ULONG Id,
    _Out_writes_bytes_(Length) PVOID Data,
    _In_ ULONG Length,
    _Out_opt_ ULONG* Returned)
{
    const DsPropertyItem* Item;
    ULONG Size = Length;
    HRESULT hr;

    if (Set == nullptr || Data == nullptr)
        return E_INVALIDARG;

    Item = DsFindProperty(Tables, TableCount, *Set, Id);
    if (Item == nullptr || Item->Get == nullptr)
    {
        hr = E_NOTIMPL;
    }
    else if (Item->Minimum != 0 && Size < Item->Minimum)
    {
        hr = Size != 0 ? E_INVALIDARG : S_OK;
        Size = Item->Minimum;
    }
    else
    {
        hr = Item->Get(Owner, Data, &Size);
    }
    if (Returned != nullptr)
        *Returned = Size;
    return hr;
}

HRESULT
DsPropertySet(
    _In_reads_(TableCount) const DsPropertyTable* Tables,
    _In_ ULONG TableCount,
    _In_opt_ PVOID Owner,
    _In_ const GUID* Set,
    _In_ ULONG Id,
    _In_reads_bytes_opt_(Length) PVOID Data,
    _In_ ULONG Length)
{
    const DsPropertyItem* Item;

    if (Set == nullptr)
        return E_INVALIDARG;
    if (Data == nullptr)
        Length = 0;

    Item = DsFindProperty(Tables, TableCount, *Set, Id);
    if (Item == nullptr || Item->Set == nullptr)
        return E_NOTIMPL;
    if (Length < Item->Minimum)
        return E_INVALIDARG;
    return Item->Set(Owner, Length != 0 ? Data : nullptr, Length);
}

/* Buffers carry no property sets, so every property is unimplemented once the arguments pass */
HRESULT
STDMETHODCALLTYPE
DsPropertyFace::Get(
    _In_ REFGUID Set,
    ULONG Id,
    _In_reads_bytes_opt_(InstanceLength) LPVOID Instance,
    ULONG InstanceLength,
    _Out_writes_bytes_(DataLength) LPVOID Data,
    ULONG DataLength,
    _Out_opt_ PULONG Returned)
{
    DsApiGuard Guard;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(InstanceLength);
    return DsPropertyGet(nullptr, 0, nullptr, &Set, Id, Data, DataLength, Returned);
}

HRESULT
STDMETHODCALLTYPE
DsPropertyFace::Set(
    _In_ REFGUID Set,
    ULONG Id,
    _In_reads_bytes_opt_(InstanceLength) LPVOID Instance,
    ULONG InstanceLength,
    _In_reads_bytes_(DataLength) LPVOID Data,
    ULONG DataLength)
{
    DsApiGuard Guard;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(InstanceLength);
    return DsPropertySet(nullptr, 0, nullptr, &Set, Id, Data, DataLength);
}

HRESULT
STDMETHODCALLTYPE
DsPropertyFace::QuerySupport(
    _In_ REFGUID Set,
    ULONG Id,
    _Out_ PULONG Support)
{
    DsApiGuard Guard;

    return DsPropertyQuery(nullptr, 0, &Set, Id, Support);
}
