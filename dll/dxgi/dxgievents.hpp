/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Cookie based registrations for DXGI status events and window messages
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

/*
 * Every notification DXGI offers works the same way: an event or a window message
 * is registered, the caller gets the smallest free cookie, and the cookie undoes it.
 */
class RxEventRegistry
{
public:
    RxEventRegistry() = default;
    ~RxEventRegistry();

    RxEventRegistry(const RxEventRegistry&) = delete;
    RxEventRegistry& operator=(const RxEventRegistry&) = delete;

    /* NotifyNow signals the event once as it is added, as status events do */
    HRESULT
    AddEvent(
        _In_ HANDLE Event,
        _Out_ DWORD* Cookie,
        _In_ BOOLEAN NotifyNow);

    HRESULT
    AddWindow(
        _In_ HWND Window,
        _In_ UINT Message,
        _Out_ DWORD* Cookie);

    /* FALSE when no registration has that cookie */
    BOOLEAN
    Remove(
        _In_ DWORD Cookie);

    VOID
    NotifyAll();

private:
    struct Entry
    {
        DWORD Cookie;
        HANDLE Original;
        HANDLE Event;
        HWND Window;
        UINT Message;
    };

    HRESULT
    Insert(
        _In_ const Entry& NewEntry,
        _Out_ DWORD* Cookie,
        _In_ BOOLEAN NotifyNow);

    static
    VOID
    Notify(
        _In_ const Entry& Target);

    SRWLOCK Lock = SRWLOCK_INIT;
    std::vector<Entry> Entries;
};
