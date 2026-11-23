/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3DPERF markers, forwarded to a capture tool when one is loaded
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

typedef int (WINAPI* PD9_BEGIN_EVENT)(D3DCOLOR, LPCWSTR);
typedef int (WINAPI* PD9_END_EVENT)(VOID);
typedef VOID (WINAPI* PD9_MARKER)(D3DCOLOR, LPCWSTR);
typedef VOID** (WINAPI* PD9_REAL_SLOT)(VOID);

static LONG D9PerfProbed;
static LONG D9PerfDepth;
static PD9_BEGIN_EVENT volatile D9PerfBegin;
static PD9_END_EVENT volatile D9PerfEnd;
static PD9_MARKER volatile D9PerfMarker;
static PD9_MARKER volatile D9PerfRegion;

/* What the capture tool calls back into while it records: a nesting count and nothing else */
static
int
WINAPI
D9PerfStubBegin(
    _In_ D3DCOLOR Color,
    _In_opt_ LPCWSTR Name)
{
    return InterlockedIncrement(&D9PerfDepth);
}

static
int
WINAPI
D9PerfStubEnd(VOID)
{
    LONG Depth = InterlockedDecrement(&D9PerfDepth);

    if (Depth < -1)
    {
        InterlockedIncrement(&D9PerfDepth);
        Depth = 0;
    }
    return Depth + 1;
}

static
VOID
WINAPI
D9PerfStubMarker(
    _In_ D3DCOLOR Color,
    _In_opt_ LPCWSTR Name)
{
}

/* Hands the tool our stub through its slot export, then takes its recording entry point */
static
VOID
D9PerfHook(
    _In_ HMODULE Tool,
    _In_z_ PCSTR SlotName,
    _In_ VOID* Stub,
    _Inout_ VOID* volatile* Target,
    _In_z_ PCSTR EntryName)
{
    PD9_REAL_SLOT Slot = reinterpret_cast<PD9_REAL_SLOT>(GetProcAddress(Tool, SlotName));

    if (Slot == nullptr)
        return;

    *Slot() = Stub;
    InterlockedExchangePointer(Target, reinterpret_cast<VOID*>(GetProcAddress(Tool, EntryName)));
}

static
VOID
D9PerfProbe(VOID)
{
    HMODULE Tool;

    if (InterlockedCompareExchange(&D9PerfProbed, 1, 0) != 0)
        return;

    Tool = GetModuleHandleW(L"DXCaptureReplay.dll");
    if (Tool == NULL)
        return;

    D9PerfHook(Tool, "GetRealPtrPtrD3DPERF_BeginEvent", reinterpret_cast<VOID*>(D9PerfStubBegin),
               reinterpret_cast<VOID* volatile*>(&D9PerfBegin), "D3DPERF_BeginEventGenerated");
    D9PerfHook(Tool, "GetRealPtrPtrD3DPERF_EndEvent", reinterpret_cast<VOID*>(D9PerfStubEnd),
               reinterpret_cast<VOID* volatile*>(&D9PerfEnd), "D3DPERF_EndEventGenerated");
    D9PerfHook(Tool, "GetRealPtrPtrD3DPERF_SetMarker", reinterpret_cast<VOID*>(D9PerfStubMarker),
               reinterpret_cast<VOID* volatile*>(&D9PerfMarker), "D3DPERF_SetMarkerGenerated");
    D9PerfHook(Tool, "GetRealPtrPtrD3DPERF_SetRegion", reinterpret_cast<VOID*>(D9PerfStubMarker),
               reinterpret_cast<VOID* volatile*>(&D9PerfRegion), "D3DPERF_SetRegionGenerated");
}

int
WINAPI
D3DPERF_BeginEvent(
    _In_ D3DCOLOR Color,
    _In_opt_ LPCWSTR Name)
{
    PD9_BEGIN_EVENT Begin;

    D9PerfProbe();
    Begin = D9PerfBegin;
    if (Begin == nullptr)
        return -1;
    return Begin(Color, Name);
}

int
WINAPI
D3DPERF_EndEvent(VOID)
{
    PD9_END_EVENT End;

    D9PerfProbe();
    End = D9PerfEnd;
    if (End == nullptr)
        return -1;
    return End();
}

VOID
WINAPI
D3DPERF_SetMarker(
    _In_ D3DCOLOR Color,
    _In_opt_ LPCWSTR Name)
{
    PD9_MARKER Marker;

    D9PerfProbe();
    Marker = D9PerfMarker;
    if (Marker != nullptr)
        Marker(Color, Name);
}

VOID
WINAPI
D3DPERF_SetRegion(
    _In_ D3DCOLOR Color,
    _In_opt_ LPCWSTR Name)
{
    PD9_MARKER Region;

    D9PerfProbe();
    Region = D9PerfRegion;
    if (Region != nullptr)
        Region(Color, Name);
}

/* Reports whether a tool that understands the markers is recording */
DWORD
WINAPI
D3DPERF_GetStatus(VOID)
{
    D9PerfProbe();
    return D9PerfBegin != nullptr;
}

BOOL
WINAPI
D3DPERF_QueryRepeatFrame(VOID)
{
    return FALSE;
}

/* Also exported as DebugSetLevel and DebugSetMute; the options have no effect any more */
VOID
WINAPI
D3DPERF_SetOptions(
    _In_ DWORD Options)
{
}
