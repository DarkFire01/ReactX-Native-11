/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Objects whose interfaces each keep their own reference count
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

extern LONG DsLiveObjects;

DsObject::DsObject() : References(1), EntryCount(0)
{
    InterlockedIncrement(&DsLiveObjects);
    UnknownFace.Link.Object = this;
    UnknownFace.Link.Count = 0;
    Expose(IID_IUnknown, &UnknownFace, &UnknownFace.Link);
}

DsObject::~DsObject()
{
    InterlockedDecrement(&DsLiveObjects);
}

ULONG
DsObject::Grab(VOID)
{
    return static_cast<ULONG>(InterlockedIncrement(&References));
}

ULONG
DsObject::Drop(VOID)
{
    LONG Count = InterlockedDecrement(&References);

    if (Count == 0)
        delete this;
    return static_cast<ULONG>(Count);
}

VOID
DsObject::Expose(
    _In_ REFIID Iid,
    _In_ IUnknown* Pointer,
    _In_ DsFaceLink* Link)
{
    for (UINT Index = 0; Index < EntryCount; Index++)
    {
        if (Entries[Index].Iid == Iid)
        {
            Entries[Index].Pointer = Pointer;
            Entries[Index].Link = Link;
            return;
        }
    }
    if (EntryCount == DS_MAX_INTERFACES)
        return;
    Entries[EntryCount].Iid = Iid;
    Entries[EntryCount].Pointer = Pointer;
    Entries[EntryCount].Link = Link;
    EntryCount++;
}

VOID
DsObject::Hide(
    _In_ REFIID Iid)
{
    for (UINT Index = 0; Index < EntryCount; Index++)
    {
        if (Entries[Index].Iid != Iid)
            continue;
        Entries[Index] = Entries[EntryCount - 1];
        EntryCount -= 1;
        return;
    }
}

/* A creator hands its own object reference to the first interface instead of adding one */
HRESULT
DsObject::Query(
    _In_ REFIID Iid,
    _In_ BOOLEAN Creator,
    _COM_Outptr_ void** Object)
{
    *Object = nullptr;
    for (UINT Index = 0; Index < EntryCount; Index++)
    {
        if (Entries[Index].Iid != Iid)
            continue;
        InterlockedIncrement(&Entries[Index].Link->Count);
        if (!Creator)
            Entries[Index].Link->Object->Grab();
        *Object = Entries[Index].Pointer;
        return S_OK;
    }
    return E_NOINTERFACE;
}

HRESULT
DsFaceQuery(
    _In_ DsFaceLink* Link,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object)
{
    DsApiGuard Guard;

    if (Object == nullptr)
        return E_INVALIDARG;
    return Link->Object->Query(Iid, FALSE, Object);
}

ULONG
DsFaceAddRef(
    _In_ DsFaceLink* Link)
{
    DsApiGuard Guard;
    ULONG Count = static_cast<ULONG>(InterlockedIncrement(&Link->Count));

    Link->Object->Grab();
    return Count;
}

ULONG
DsFaceRelease(
    _In_ DsFaceLink* Link)
{
    DsApiGuard Guard;
    ULONG Count;

    if (Link->Count > 0)
        Link->Count -= 1;
    Count = static_cast<ULONG>(Link->Count);
    Link->Object->Drop();
    return Count;
}
