/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Cookie based registrations for DXGI status events and window messages
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxgievents.hpp"

#include <algorithm>

/* Cookies are sixteen bits worth of registrations */
constexpr SIZE_T RxMaximumRegistrations = 0xFFFF;

RxEventRegistry::~RxEventRegistry()
{
    for (const Entry& Each : Entries)
    {
        if (Each.Event != NULL)
            CloseHandle(Each.Event);
    }
}

VOID
RxEventRegistry::Notify(
    _In_ const Entry& Target)
{
    if (Target.Event != NULL)
        SetEvent(Target.Event);
    else
        PostMessageW(Target.Window, Target.Message, 0, 0);
}

HRESULT
RxEventRegistry::Insert(
    _In_ const Entry& NewEntry,
    _Out_ DWORD* Cookie,
    _In_ BOOLEAN NotifyNow)
{
    Entry Added = NewEntry;
    SIZE_T Position;

    AcquireSRWLockExclusive(&Lock);

    for (const Entry& Each : Entries)
    {
        if ((Added.Original != NULL && Each.Original == Added.Original) ||
            (Added.Window != NULL && Each.Window == Added.Window && Each.Message == Added.Message))
        {
            ReleaseSRWLockExclusive(&Lock);
            return DXGI_ERROR_INVALID_CALL;
        }
    }

    if (Entries.size() >= RxMaximumRegistrations)
    {
        ReleaseSRWLockExclusive(&Lock);
        return E_OUTOFMEMORY;
    }

    /* The list stays sorted, so the first gap is the smallest free cookie */
    Added.Cookie = 1;
    for (Position = 0; Position < Entries.size() && Entries[Position].Cookie == Added.Cookie; Position++)
        Added.Cookie++;

    Entries.insert(Entries.begin() + Position, Added);
    *Cookie = Added.Cookie;
    if (NotifyNow)
        Notify(Added);

    ReleaseSRWLockExclusive(&Lock);
    return S_OK;
}

HRESULT
RxEventRegistry::AddEvent(
    _In_ HANDLE Event,
    _Out_ DWORD* Cookie,
    _In_ BOOLEAN NotifyNow)
{
    Entry NewEntry = {};
    HRESULT hr;

    if (Event == NULL || Cookie == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* The registry keeps its own handle, so the caller may close theirs */
    if (!DuplicateHandle(GetCurrentProcess(), Event, GetCurrentProcess(), &NewEntry.Event, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
    {
        return E_OUTOFMEMORY;
    }

    NewEntry.Original = Event;
    hr = Insert(NewEntry, Cookie, NotifyNow);
    if (FAILED(hr))
        CloseHandle(NewEntry.Event);
    return hr;
}

HRESULT
RxEventRegistry::AddWindow(
    _In_ HWND Window,
    _In_ UINT Message,
    _Out_ DWORD* Cookie)
{
    Entry NewEntry = {};
    DWORD Owner = 0;

    if (Message == 0 || Cookie == nullptr || Window == NULL || Window == HWND_BROADCAST)
        return DXGI_ERROR_INVALID_CALL;

    /* Only windows of this process take these messages */
    GetWindowThreadProcessId(Window, &Owner);
    if (Owner != GetCurrentProcessId())
        return DXGI_ERROR_INVALID_CALL;

    NewEntry.Window = Window;
    NewEntry.Message = Message;
    return Insert(NewEntry, Cookie, TRUE);
}

BOOLEAN
RxEventRegistry::Remove(
    _In_ DWORD Cookie)
{
    BOOLEAN Found = FALSE;
    SIZE_T Index;

    AcquireSRWLockExclusive(&Lock);
    for (Index = 0; Index < Entries.size(); Index++)
    {
        if (Entries[Index].Cookie != Cookie)
            continue;

        if (Entries[Index].Event != NULL)
            CloseHandle(Entries[Index].Event);
        Entries.erase(Entries.begin() + Index);
        Found = TRUE;
        break;
    }
    ReleaseSRWLockExclusive(&Lock);

    return Found;
}

VOID
RxEventRegistry::NotifyAll()
{
    AcquireSRWLockShared(&Lock);
    for (const Entry& Each : Entries)
        Notify(Each);
    ReleaseSRWLockShared(&Lock);
}
