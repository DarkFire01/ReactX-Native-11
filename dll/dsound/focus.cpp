/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Muting buffers whose application loses the foreground window
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

/* How often the foreground window is checked, in milliseconds */
#define DS_FOCUS_INTERVAL   500

/* Window placement showCmd of a minimized window */
#define DS_SHOW_MINIMIZED   2

static DsSound* DsSounds;
static DWORD DsFocusThread;
static DWORD DsFocusLevel;
static HANDLE DsFocusWorker;
static HANDLE DsFocusQuit;

static
VOID
DsStopFocus(VOID);

VOID
DsRegisterSound(
    _In_ DsSound* Sound)
{
    DsApiGuard Guard;

    Sound->NextSound = DsSounds;
    DsSounds = Sound;
}

VOID
DsUnregisterSound(
    _In_ DsSound* Sound)
{
    DsApiGuard Guard;

    for (DsSound** Link = &DsSounds; *Link != nullptr; Link = &(*Link)->NextSound)
    {
        if (*Link == Sound)
        {
            *Link = Sound->NextSound;
            break;
        }
    }
    if (DsSounds == nullptr)
        DsStopFocus();
}

/* Whether a buffer plays (0), is silenced (1) or is lost (2) with the current focus */
ULONG
DsBufferFocus(
    _In_ DsBuffer* Buffer)
{
    DsSound* Owner = Buffer->Owner;
    BOOLEAN Primary = (Buffer->Flags & DSBCAPS_PRIMARYBUFFER) != 0;

    if (DsFocusLevel == DSSCL_WRITEPRIMARY)
    {
        if (!Primary || Owner->LevelThread != DsFocusThread)
            return 2;
    }
    else if (Owner->Level == DSSCL_WRITEPRIMARY)
    {
        if (!Primary || DsFocusLevel != 0 || !(Buffer->Flags & DSBCAPS_STICKYFOCUS))
            return 2;
    }

    if (Buffer->Flags & DSBCAPS_GLOBALFOCUS)
        return 0;
    if (Owner->LevelThread == DsFocusThread)
        return 0;
    if ((Buffer->Flags & DSBCAPS_STICKYFOCUS) && DsFocusLevel == 0 && Buffer->Active)
        return 0;
    return 1;
}

VOID
DsApplyBufferFocus(
    _In_ DsSecondary* Buffer)
{
    switch (DsBufferFocus(Buffer))
    {
        case 0:
            Buffer->Active = TRUE;
            Buffer->Voice.SetFocusMute(FALSE);
            break;
        case 1:
            Buffer->Active = FALSE;
            Buffer->Voice.SetFocusMute(TRUE);
            break;
        default:
            Buffer->Lose();
            break;
    }
}

static
VOID
DsApplyFocus(VOID)
{
    for (DsSound* Sound = DsSounds; Sound != nullptr; Sound = Sound->NextSound)
    {
        if (FAILED(Sound->InitStatus))
            continue;
        for (DsSecondary* Buffer = Sound->Buffers; Buffer != nullptr; Buffer = Buffer->NextBuffer)
            DsApplyBufferFocus(Buffer);
    }
}

/* The thread owning the foreground window, unless minimized, and the highest level held on it */
static
BOOLEAN
DsReadFocus(VOID)
{
    HWND Foreground = GetForegroundWindow();
    DWORD Thread = 0;
    DWORD Level = 0;
    BOOLEAN Changed;

    if (Foreground != nullptr)
    {
        WINDOWPLACEMENT Placement = {};

        Foreground = DsRootWindow(Foreground);
        Placement.length = sizeof(Placement);
        if (IsWindow(Foreground) && GetWindowPlacement(Foreground, &Placement) && Placement.showCmd != DS_SHOW_MINIMIZED)
            Thread = GetWindowThreadProcessId(Foreground, nullptr);
    }
    if (Thread != 0)
    {
        for (DsSound* Sound = DsSounds; Sound != nullptr; Sound = Sound->NextSound)
        {
            if (Sound->LevelThread == Thread && Sound->Level > Level)
                Level = Sound->Level;
        }
    }

    Changed = Thread != DsFocusThread || Level != DsFocusLevel;
    DsFocusThread = Thread;
    DsFocusLevel = Level;
    return Changed;
}

/* Holds a module reference so the library stays loaded until the thread has left it */
static
DWORD
WINAPI
DsFocusMain(
    _In_ PVOID Context)
{
    HANDLE Quit = static_cast<HANDLE>(Context);

    while (WaitForSingleObject(Quit, DS_FOCUS_INTERVAL) == WAIT_TIMEOUT)
    {
        DsApiGuard Guard;

        if (DsReadFocus())
            DsApplyFocus();
    }
    CloseHandle(Quit);
    FreeLibraryAndExitThread(DsModule, 0);
}

/* Called with the API lock held; a forced update applies even when the focus has not moved */
VOID
DsUpdateFocus(
    _In_ BOOLEAN Force)
{
    if (DsFocusWorker == nullptr)
    {
        HMODULE Held = nullptr;

        DsFocusQuit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (DsFocusQuit != nullptr &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&DsFocusMain), &Held))
        {
            DsFocusWorker = CreateThread(nullptr, 0, DsFocusMain, DsFocusQuit, 0, nullptr);
            if (DsFocusWorker == nullptr)
                FreeLibrary(Held);
        }
        if (DsFocusWorker == nullptr)
        {
            DsTrace(L"cannot start the focus thread (%lu)", GetLastError());
            if (DsFocusQuit != nullptr)
                CloseHandle(DsFocusQuit);
            DsFocusQuit = nullptr;
        }
    }
    if (DsReadFocus() || Force)
        DsApplyFocus();
}

/* The thread closes its own event and module reference on the way out */
static
VOID
DsStopFocus(VOID)
{
    if (DsFocusWorker == nullptr)
        return;
    SetEvent(DsFocusQuit);
    CloseHandle(DsFocusWorker);
    DsFocusWorker = nullptr;
    DsFocusQuit = nullptr;
}
