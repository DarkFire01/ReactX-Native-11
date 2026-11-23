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

/* Every process shares the levels its DirectSound objects hold, so focus can follow a window of another process */
#define DS_SHARED_NAME          L"DirectSound Administrator shared thread array"
#define DS_SHARED_LOCK_NAME     L"DirectSound Administrator shared thread array (lock)"
#define DS_SHARED_MAX_THREADS   1024

/* One DirectSound object of some process: the thread it took a level for and that level */
struct DsSharedThread
{
    DWORD Process;
    DWORD Thread;
    DWORD Level;
};

/* The mapping starts with the byte size of the entries that follow it */
struct DsSharedList
{
    DWORD Bytes;
    DsSharedThread Threads[DS_SHARED_MAX_THREADS];
};

static DsSound* DsSounds;
static DWORD DsFocusThread;
static DWORD DsFocusLevel;
static HANDLE DsFocusWorker;
static HANDLE DsFocusQuit;
static HANDLE DsSharedMapping;
static HANDLE DsSharedLock;

static
VOID
DsStopFocus(VOID);

/* Opens the list other processes made, or makes an empty one; called with the API lock held */
static
VOID
DsOpenShared(VOID)
{
    if (DsSharedLock == nullptr)
    {
        DsSharedLock = CreateMutexW(nullptr, FALSE, DS_SHARED_LOCK_NAME);
        if (DsSharedLock == nullptr)
        {
            DsTrace(L"cannot open the shared thread list lock (%lu)", GetLastError());
            return;
        }
    }
    if (DsSharedMapping != nullptr)
        return;

    WaitForSingleObject(DsSharedLock, INFINITE);
    DsSharedMapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, DS_SHARED_NAME);
    if (DsSharedMapping == nullptr)
    {
        DsSharedMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DsSharedList), DS_SHARED_NAME);
        if (DsSharedMapping != nullptr)
        {
            DWORD* Bytes = static_cast<DWORD*>(MapViewOfFile(DsSharedMapping, FILE_MAP_WRITE, 0, 0, sizeof(*Bytes)));

            if (Bytes != nullptr)
            {
                *Bytes = 0;
                UnmapViewOfFile(Bytes);
            }
        }
        else
        {
            DsTrace(L"cannot make the shared thread list (%lu)", GetLastError());
        }
    }
    ReleaseMutex(DsSharedLock);
}

/* Copies the shared entries out; called with the shared lock held */
static
ULONG
DsReadShared(
    _Out_writes_(DS_SHARED_MAX_THREADS) DsSharedThread* Threads)
{
    const DsSharedList* List;
    ULONG Count;

    if (DsSharedMapping == nullptr)
        return 0;
    List = static_cast<const DsSharedList*>(MapViewOfFile(DsSharedMapping, FILE_MAP_READ, 0, 0, sizeof(*List)));
    if (List == nullptr)
        return 0;
    Count = DsMin<ULONG>(List->Bytes / sizeof(DsSharedThread), DS_SHARED_MAX_THREADS);
    memcpy(Threads, List->Threads, Count * sizeof(*Threads));
    UnmapViewOfFile(List);
    return Count;
}

/* Replaces this process's entries with the thread and level of each of its objects; called with the API lock held */
static
VOID
DsPublishLevels(VOID)
{
    DsSharedThread* Threads;
    DWORD Self = GetCurrentProcessId();
    ULONG Count;
    ULONG Kept = 0;

    DsOpenShared();
    if (DsSharedMapping == nullptr)
        return;
    Threads = static_cast<DsSharedThread*>(HeapAlloc(GetProcessHeap(), 0, DS_SHARED_MAX_THREADS * sizeof(*Threads)));
    if (Threads == nullptr)
        return;

    WaitForSingleObject(DsSharedLock, INFINITE);
    Count = DsReadShared(Threads);
    for (ULONG Index = 0; Index < Count; Index++)
    {
        if (Threads[Index].Process != Self)
            Threads[Kept++] = Threads[Index];
    }
    for (DsSound* Sound = DsSounds; Sound != nullptr && Kept < DS_SHARED_MAX_THREADS; Sound = Sound->NextSound)
    {
        Threads[Kept].Process = Self;
        Threads[Kept].Thread = Sound->LevelThread;
        Threads[Kept].Level = Sound->Level != 0 ? Sound->Level : DSSCL_NORMAL;
        Kept += 1;
    }

    /* An empty list is never written, so the last entries stay behind once every object is gone */
    if (Kept != 0)
    {
        DsSharedList* List = static_cast<DsSharedList*>(MapViewOfFile(DsSharedMapping, FILE_MAP_WRITE, 0, 0, sizeof(*List)));

        if (List != nullptr)
        {
            List->Bytes = Kept * sizeof(*Threads);
            memcpy(List->Threads, Threads, Kept * sizeof(*Threads));
            UnmapViewOfFile(List);
        }
    }
    ReleaseMutex(DsSharedLock);
    HeapFree(GetProcessHeap(), 0, Threads);
}

/* The highest level any process holds on a thread */
static
DWORD
DsSharedLevel(
    _In_ DWORD Thread)
{
    DsSharedThread* Threads;
    DWORD Level = 0;
    ULONG Count;

    if (DsSharedMapping == nullptr)
    {
        for (DsSound* Sound = DsSounds; Sound != nullptr; Sound = Sound->NextSound)
        {
            if (Sound->LevelThread == Thread && Sound->Level > Level)
                Level = Sound->Level;
        }
        return Level;
    }
    Threads = static_cast<DsSharedThread*>(HeapAlloc(GetProcessHeap(), 0, DS_SHARED_MAX_THREADS * sizeof(*Threads)));
    if (Threads == nullptr)
        return 0;
    WaitForSingleObject(DsSharedLock, INFINITE);
    Count = DsReadShared(Threads);
    ReleaseMutex(DsSharedLock);
    for (ULONG Index = 0; Index < Count; Index++)
    {
        if (Threads[Index].Thread == Thread && Threads[Index].Level > Level)
            Level = Threads[Index].Level;
    }
    HeapFree(GetProcessHeap(), 0, Threads);
    return Level;
}

/* Called with the API lock held, after an object's thread or level changed */
VOID
DsLevelsChanged(VOID)
{
    DsPublishLevels();
}

VOID
DsRegisterSound(
    _In_ DsSound* Sound)
{
    DsApiGuard Guard;
    DsSound** Link = &DsSounds;

    /* Oldest first, the order the object list property reports them in */
    while (*Link != nullptr)
        Link = &(*Link)->NextSound;
    Sound->NextSound = nullptr;
    *Link = Sound;
    DsPublishLevels();
}

/* Called with the API lock held */
DsSound*
DsFirstSound(VOID)
{
    return DsSounds;
}

/* Finds the device behind a buffer interface pointer of any DirectSound object in the process */
BOOLEAN
DsFindBufferDevice(
    _In_opt_ const VOID* Interface,
    _Out_ GUID* Device)
{
    *Device = GUID_NULL;
    if (Interface == nullptr)
        return FALSE;
    for (DsSound* Sound = DsSounds; Sound != nullptr; Sound = Sound->NextSound)
    {
        BOOLEAN Found = Sound->Primary != nullptr && Interface == &Sound->Primary->Face;

        for (DsSecondary* Buffer = Sound->Buffers; Buffer != nullptr && !Found; Buffer = Buffer->NextBuffer)
            Found = Interface == &Buffer->Face;
        if (Found && Sound->Device != nullptr)
        {
            *Device = Sound->Device->Id;
            return TRUE;
        }
    }
    return FALSE;
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
    DsPublishLevels();
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
        Level = DsSharedLevel(Thread);

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
