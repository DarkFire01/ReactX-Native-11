/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Display mode tables and the mode enumeration methods
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

/* The legacy table starts with room for this many modes and grows by the second count */
static constexpr UINT D9LegacyTableStart = 384;
static constexpr UINT D9LegacyTableStep = 32;

/* Television rates are listed both ways; 29 and 59 count as 30 and 60 */
static
UINT
D9NominalRate(
    _In_ UINT Rate)
{
    if (Rate == 29)
        return 30;
    if (Rate == 59)
        return 60;
    return Rate;
}

static
D3DSCANLINEORDERING
D9ScanLineFromDevMode(
    _In_ const DEVMODEA* DevMode)
{
    if ((DevMode->dmFields & DM_DISPLAYFLAGS) == 0)
        return D3DSCANLINEORDERING_UNKNOWN;
    return (DevMode->dmDisplayFlags & DM_INTERLACED) != 0 ? D3DSCANLINEORDERING_INTERLACED
                                                           : D3DSCANLINEORDERING_PROGRESSIVE;
}

/* Tells a 555 desktop from a 565 one by the green mask GDI reports for a compatible bitmap */
static
D3DFORMAT
D9SixteenBitDesktopFormat(
    _In_z_ PCSTR DeviceName)
{
    D3DFORMAT Format = D3DFMT_R5G6B5;
    BITMAPINFO* Info;
    HBITMAP Bitmap;
    BOOLEAN IsDisplay;
    HDC Dc;

    IsDisplay = DeviceName[0] == '\\' && DeviceName[1] == '\\' && DeviceName[2] == '.';
    Dc = CreateDCA(IsDisplay ? nullptr : DeviceName, IsDisplay ? DeviceName : nullptr, nullptr, nullptr);
    if (Dc == NULL)
        return Format;

    Info = static_cast<BITMAPINFO*>(LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER) + 4 * sizeof(DWORD)));
    Bitmap = Info != nullptr ? CreateCompatibleBitmap(Dc, 1, 1) : NULL;
    if (Bitmap != NULL)
    {
        Info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        if (GetDIBits(Dc, Bitmap, 0, 0, nullptr, Info, DIB_RGB_COLORS) &&
            Info->bmiHeader.biCompression == BI_BITFIELDS)
        {
            GetDIBits(Dc, Bitmap, 0, Info->bmiHeader.biHeight, nullptr, Info, DIB_RGB_COLORS);
            if (reinterpret_cast<const DWORD*>(Info->bmiColors)[1] == 0x3E0)
                Format = D3DFMT_X1R5G5B5;
        }
        DeleteObject(Bitmap);
    }
    LocalFree(Info);
    DeleteDC(Dc);
    return Format;
}

/* The desktop mode GDI reports, in the form Direct3D 9 reports it */
HRESULT
D9ReadDesktopMode(
    _In_z_ PCSTR DeviceName,
    _Out_ D3DDISPLAYMODEEX* Mode,
    _Out_opt_ D3DDDI_ROTATION* Rotation)
{
    DEVMODEA DevMode = {};

    DevMode.dmSize = sizeof(DevMode);
    if (!EnumDisplaySettingsA(DeviceName, ENUM_CURRENT_SETTINGS, &DevMode))
        return D3DERR_NOTFOUND;

    /* Without a frequency field the runtime reports the display flags word in its place */
    Mode->Size = sizeof(*Mode);
    Mode->Width = DevMode.dmPelsWidth;
    Mode->Height = DevMode.dmPelsHeight;
    Mode->RefreshRate = DevMode.dmDisplayFlags;
    if ((DevMode.dmFields & DM_DISPLAYFREQUENCY) != 0)
        Mode->RefreshRate = DevMode.dmDisplayFrequency;
    Mode->ScanLineOrdering = D9ScanLineFromDevMode(&DevMode);

    switch (DevMode.dmBitsPerPel)
    {
    case 8:
        Mode->Format = D3DFMT_P8;
        break;
    case 15:
    case 16:
        Mode->Format = D9SixteenBitDesktopFormat(DeviceName);
        break;
    case 24:
        Mode->Format = D3DFMT_R8G8B8;
        break;
    case 32:
        Mode->Format = D3DFMT_X8R8G8B8;
        break;
    default:
        Mode->Format = D3DFMT_UNKNOWN;
        break;
    }

    if (Rotation != nullptr)
    {
        *Rotation = D3DDDI_ROTATION_IDENTITY;
        if ((DevMode.dmFields & DM_DISPLAYORIENTATION) != 0)
        {
            if (DevMode.dmDisplayOrientation == DMDO_90)
                *Rotation = D3DDDI_ROTATION_90;
            else if (DevMode.dmDisplayOrientation == DMDO_180)
                *Rotation = D3DDDI_ROTATION_180;
            else if (DevMode.dmDisplayOrientation == DMDO_270)
                *Rotation = D3DDDI_ROTATION_270;
        }
    }
    return S_OK;
}

/* Desktop formats with alpha are listed as their opaque twins */
static
D3DDDIFORMAT
D9OpaqueDesktopFormat(
    _In_ D3DDDIFORMAT Format)
{
    switch (Format)
    {
    case D3DDDIFMT_A1R5G5B5:
        return D3DDDIFMT_X1R5G5B5;
    case D3DDDIFMT_A8R8G8B8:
        return D3DDDIFMT_X8R8G8B8;
    case D3DDDIFMT_A4R4G4B4:
        return D3DDDIFMT_X4R4G4B4;
    case D3DDDIFMT_A8B8G8R8:
        return D3DDDIFMT_X8B8G8R8;
    default:
        return Format;
    }
}

static
BOOLEAN
D9SameKernelMode(
    _In_ const D3DKMT_DISPLAYMODE* Left,
    _In_ const D3DKMT_DISPLAYMODE* Right)
{
    return Left->Width == Right->Width && Left->Height == Right->Height && Left->Format == Right->Format &&
           Left->ScanLineOrdering == Right->ScanLineOrdering && Left->DisplayOrientation == Right->DisplayOrientation &&
           D9NominalRate(Left->IntegerRefreshRate) == D9NominalRate(Right->IntegerRefreshRate);
}

/* The kernel's mode list of each display, shared by every live Direct3D object of the process */
struct D9KernelModeList
{
    D3DKMT_DISPLAYMODE* Modes;
    UINT Count;
};

static D9KernelModeList D9KernelModeLists[D9MaxAdapters];
static ULONG D9KernelModeUsers;
static SRWLOCK D9KernelModeLock = SRWLOCK_INIT;

VOID
D9AcquireKernelModes(VOID)
{
    AcquireSRWLockExclusive(&D9KernelModeLock);
    if (D9KernelModeUsers == 0)
        ZeroMemory(D9KernelModeLists, sizeof(D9KernelModeLists));
    D9KernelModeUsers += 1;
    ReleaseSRWLockExclusive(&D9KernelModeLock);
}

VOID
D9ReleaseKernelModes(VOID)
{
    AcquireSRWLockExclusive(&D9KernelModeLock);
    D9KernelModeUsers -= 1;
    if (D9KernelModeUsers == 0)
    {
        for (D9KernelModeList& List : D9KernelModeLists)
        {
            delete[] List.Modes;
            List.Modes = nullptr;
            List.Count = 0;
        }
    }
    ReleaseSRWLockExclusive(&D9KernelModeLock);
}

/* Asks the kernel for the display's modes; the list may grow between the two calls */
static
NTSTATUS
D9FetchKernelModes(
    _In_ const D9Adapter* Adapter,
    _Out_ D9KernelModeList* Result)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_GETDISPLAYMODELIST List = {};
    NTSTATUS Status;

    Result->Modes = nullptr;
    Result->Count = 0;
    if (Kmt == nullptr || Kmt->GetDisplayModeList == nullptr)
        return STATUS_NOT_SUPPORTED;

    List.hAdapter = Adapter->KernelAdapter;
    List.VidPnSourceId = Adapter->SourceId;
    Status = Kmt->GetDisplayModeList(&List);
    while (NT_SUCCESS(Status) && List.ModeCount != 0)
    {
        List.pModeList = new (std::nothrow) D3DKMT_DISPLAYMODE[List.ModeCount]();
        if (List.pModeList == nullptr)
            return STATUS_NO_MEMORY;

        Status = Kmt->GetDisplayModeList(&List);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;
        delete[] List.pModeList;
        List.pModeList = nullptr;
    }
    if (!NT_SUCCESS(Status))
    {
        delete[] List.pModeList;
        return Status;
    }

    Result->Modes = List.pModeList;
    Result->Count = List.pModeList != nullptr ? List.ModeCount : 0;
    return STATUS_SUCCESS;
}

/* The kernel's modes for the current orientation, stereo modes and duplicates removed */
HRESULT
D9ReadKernelModes(
    _In_ const D9Adapter* Adapter,
    _In_ BOOLEAN Refresh,
    _Outptr_result_buffer_maybenull_(*Count) D3DKMT_DISPLAYMODE** Modes,
    _Out_ UINT* Count)
{
    D9KernelModeList* Cached = &D9KernelModeLists[Adapter->Ordinal];
    D3DKMT_DISPLAYMODE* Kept;
    UINT KeptCount = 0;
    NTSTATUS Status;

    *Modes = nullptr;
    *Count = 0;

    AcquireSRWLockExclusive(&D9KernelModeLock);
    if (Refresh || Cached->Modes == nullptr)
    {
        delete[] Cached->Modes;
        Status = D9FetchKernelModes(Adapter, Cached);
        if (!NT_SUCCESS(Status))
        {
            ReleaseSRWLockExclusive(&D9KernelModeLock);
            D9Trace(L"D3DKMTGetDisplayModeList failed 0x%08lX", Status);
            return Status == STATUS_NOT_SUPPORTED ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
                                                  : D9HresultFromStatus(Status);
        }
    }
    if (Cached->Modes == nullptr)
    {
        ReleaseSRWLockExclusive(&D9KernelModeLock);
        return S_OK;
    }

    Kept = new (std::nothrow) D3DKMT_DISPLAYMODE[Cached->Count];
    if (Kept == nullptr)
    {
        ReleaseSRWLockExclusive(&D9KernelModeLock);
        return E_OUTOFMEMORY;
    }

    for (UINT Index = 0; Index < Cached->Count; Index++)
    {
        D3DKMT_DISPLAYMODE Mode = Cached->Modes[Index];
        BOOLEAN Duplicate = FALSE;

        if (Mode.Flags.Stereo || Mode.DisplayOrientation != Adapter->Rotation)
            continue;

        if (Mode.DisplayOrientation == D3DDDI_ROTATION_90 || Mode.DisplayOrientation == D3DDDI_ROTATION_270)
            std::swap(Mode.Width, Mode.Height);
        Mode.Format = D9OpaqueDesktopFormat(Mode.Format);

        for (UINT Earlier = 0; Earlier < KeptCount && !Duplicate; Earlier++)
            Duplicate = D9SameKernelMode(&Kept[Earlier], &Mode);
        if (!Duplicate)
        {
            Kept[KeptCount] = Mode;
            KeptCount += 1;
        }
    }

    ReleaseSRWLockExclusive(&D9KernelModeLock);
    *Modes = Kept;
    *Count = KeptCount;
    return S_OK;
}

/* One kernel mode as a mode table entry; modes without a known scan line order are left out */
static
BOOLEAN
D9TableEntryFromKernel(
    _In_ const D3DKMT_DISPLAYMODE* Kernel,
    _Out_ D3DDISPLAYMODEEX* Mode)
{
    Mode->Size = sizeof(*Mode);
    Mode->Width = Kernel->Width;
    Mode->Height = Kernel->Height;
    Mode->Format = static_cast<D3DFORMAT>(Kernel->Format);
    Mode->RefreshRate = Kernel->IntegerRefreshRate;

    switch (Kernel->ScanLineOrdering)
    {
    case D3DDDI_VSSLO_PROGRESSIVE:
        Mode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
        return TRUE;
    case D3DDDI_VSSLO_INTERLACED_UPPERFIELDFIRST:
    case D3DDDI_VSSLO_INTERLACED_LOWERFIELDFIRST:
        Mode->ScanLineOrdering = D3DSCANLINEORDERING_INTERLACED;
        return TRUE;
    default:
        return FALSE;
    }
}

/* The extended runtime lists the kernel's validated modes, then the ones not checked against the monitor */
static
HRESULT
D9BuildKernelTable(
    _Inout_ D9Adapter* Adapter)
{
    D3DKMT_DISPLAYMODE* Kernel;
    UINT Count;
    HRESULT hr;

    hr = D9ReadKernelModes(Adapter, TRUE, &Kernel, &Count);
    if (FAILED(hr))
        return hr;

    Adapter->Modes = new (std::nothrow) D3DDISPLAYMODEEX[Count + 1];
    if (Adapter->Modes == nullptr)
    {
        delete[] Kernel;
        return E_OUTOFMEMORY;
    }

    for (UINT Index = 0; Index < Count; Index++)
    {
        if (!Kernel[Index].Flags.ValidatedAgainstMonitorCaps)
        {
            Adapter->UnvalidatedModeCount += 1;
            continue;
        }
        if (D9TableEntryFromKernel(&Kernel[Index], &Adapter->Modes[Adapter->ModeCount]))
            Adapter->ModeCount += 1;
    }

    if (Adapter->UnvalidatedModeCount != 0)
    {
        UINT Next = Adapter->ModeCount;

        for (UINT Index = 0; Index < Count; Index++)
        {
            if (!Kernel[Index].Flags.ValidatedAgainstMonitorCaps &&
                D9TableEntryFromKernel(&Kernel[Index], &Adapter->Modes[Next]))
            {
                Next += 1;
            }
        }
    }

    delete[] Kernel;
    return S_OK;
}

/* The plain runtime walks GDI's mode list and keeps the depths the driver can scan out */
static
HRESULT
D9BuildGdiTable(
    _Inout_ D9Adapter* Adapter)
{
    DEVMODEA DevMode = {};
    UINT Capacity = 0;
    UINT Count = 0;

    for (DWORD Index = 0;; Index++)
    {
        D3DDISPLAYMODEEX* Mode;
        D3DFORMAT Format;

        if (Count >= Capacity)
        {
            UINT Larger = Capacity == 0 ? D9LegacyTableStart + D9LegacyTableStep : Capacity + D9LegacyTableStep;
            D3DDISPLAYMODEEX* Grown = new (std::nothrow) D3DDISPLAYMODEEX[Larger]();

            if (Grown == nullptr)
            {
                D9FreeModeTable(Adapter);
                return E_OUTOFMEMORY;
            }
            if (Adapter->Modes != nullptr)
                memcpy(Grown, Adapter->Modes, Count * sizeof(*Grown));
            delete[] Adapter->Modes;
            Adapter->Modes = Grown;
            Capacity = Larger;
        }

        DevMode.dmSize = sizeof(DevMode);
        if (!EnumDisplaySettingsA(Adapter->DeviceName, Index, &DevMode))
            break;

        if (DevMode.dmBitsPerPel == 15 || DevMode.dmBitsPerPel == 16)
        {
            if (!Adapter->Displays16)
                continue;
            Format = Adapter->Display16Format;
        }
        else if (DevMode.dmBitsPerPel == 32)
        {
            if (!Adapter->Displays32)
                continue;
            Format = D3DFMT_X8R8G8B8;
        }
        else
        {
            continue;
        }

        Mode = &Adapter->Modes[Count];
        Mode->Size = sizeof(*Mode);
        Mode->Width = DevMode.dmPelsWidth;
        Mode->Height = DevMode.dmPelsHeight;
        Mode->RefreshRate = (DevMode.dmFields & DM_DISPLAYFREQUENCY) != 0 ? DevMode.dmDisplayFrequency : 0;
        Mode->Format = Format;
        Mode->ScanLineOrdering = D9ScanLineFromDevMode(&DevMode);
        Count += 1;
    }

    Adapter->ModeCount = Count;
    Adapter->UnvalidatedModeCount = Capacity - Count;
    return S_OK;
}

/* Gives each 10 bit or float desktop mode the 32 bit GDI mode it matches, in the wide format */
static
HRESULT
D9AddWideCopies(
    _Inout_ D9Adapter* Adapter)
{
    UINT Matches = 0;
    UINT Next;

    for (UINT Wide = 0; Wide < Adapter->WideModeCount; Wide++)
    {
        for (UINT Index = 0; Index < Adapter->ModeCount; Index++)
        {
            const D3DDISPLAYMODEEX* Mode = &Adapter->Modes[Index];

            if (Mode->Format == D3DFMT_X8R8G8B8 && Mode->Width == Adapter->WideModes[Wide].Width &&
                Mode->Height == Adapter->WideModes[Wide].Height &&
                D9NominalRate(Mode->RefreshRate) == D9NominalRate(Adapter->WideModes[Wide].RefreshRate))
            {
                Matches += 1;
                break;
            }
        }
    }
    if (Matches == 0)
        return S_OK;

    if (Matches > Adapter->UnvalidatedModeCount)
    {
        D3DDISPLAYMODEEX* Grown = new (std::nothrow) D3DDISPLAYMODEEX[Adapter->ModeCount + Matches];

        if (Grown == nullptr)
        {
            D9FreeModeTable(Adapter);
            return E_OUTOFMEMORY;
        }
        memcpy(Grown, Adapter->Modes, Adapter->ModeCount * sizeof(*Grown));
        delete[] Adapter->Modes;
        Adapter->Modes = Grown;
    }

    Next = Adapter->ModeCount;
    for (UINT Wide = 0; Wide < Adapter->WideModeCount; Wide++)
    {
        for (UINT Index = 0; Index < Adapter->ModeCount; Index++)
        {
            const D3DDISPLAYMODEEX* Mode = &Adapter->Modes[Index];

            if (Mode->Format == D3DFMT_X8R8G8B8 && Mode->Width == Adapter->WideModes[Wide].Width &&
                Mode->Height == Adapter->WideModes[Wide].Height &&
                D9NominalRate(Mode->RefreshRate) == D9NominalRate(Adapter->WideModes[Wide].RefreshRate))
            {
                Adapter->Modes[Next] = *Mode;
                Adapter->Modes[Next].Format = Adapter->WideModes[Wide].Format;
                Next += 1;
                break;
            }
        }
    }
    Adapter->ExtendedModeCount = Next - Adapter->ModeCount;
    return S_OK;
}

VOID
D9FreeModeTable(
    _Inout_ D9Adapter* Adapter)
{
    delete[] Adapter->Modes;
    Adapter->Modes = nullptr;
    Adapter->ModeCount = 0;
    Adapter->ExtendedModeCount = 0;
    Adapter->UnvalidatedModeCount = 0;
}

static
HRESULT
D9BuildModeTable(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx)
{
    HRESULT hr;

    D9FreeModeTable(Adapter);
    if (IsEx)
    {
        hr = D9BuildKernelTable(Adapter);
        if (hr != HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
            return hr;
        D9FreeModeTable(Adapter);
    }

    hr = D9BuildGdiTable(Adapter);
    if (FAILED(hr) || IsEx)
        return hr;
    return D9AddWideCopies(Adapter);
}

/* Desktop formats a mode filter may name */
BOOLEAN
D9IsDesktopFormat(
    _In_ D3DFORMAT Format,
    _In_ BOOLEAN IsEx)
{
    switch (Format)
    {
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_A2B10G10R10_XR_BIAS:
        return IsEx;
    case D3DFMT_R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_P8:
        return TRUE;
    default:
        return FALSE;
    }
}

static
BOOLEAN
D9IsFilterValid(
    _In_opt_ const D3DDISPLAYMODEFILTER* Filter)
{
    return Filter != nullptr && Filter->Size == sizeof(*Filter) && Filter->Format != D3DFMT_UNKNOWN &&
           Filter->ScanLineOrdering <= D3DSCANLINEORDERING_INTERLACED;
}

/* Unknown in the filter matches either known order; a mode of unknown order never matches */
static
BOOLEAN
D9ModeMatches(
    _In_ const D3DDISPLAYMODEFILTER* Filter,
    _In_ const D3DDISPLAYMODEEX* Mode)
{
    if (Filter->Format != Mode->Format || Mode->ScanLineOrdering == D3DSCANLINEORDERING_UNKNOWN)
        return FALSE;
    return Filter->ScanLineOrdering == Mode->ScanLineOrdering ||
           Filter->ScanLineOrdering == D3DSCANLINEORDERING_UNKNOWN;
}

/* The part of the table a filter's format looks at */
static
HRESULT
D9ModeRange(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx,
    _In_ D3DFORMAT Format,
    _Outptr_result_buffer_(*Count) const D3DDISPLAYMODEEX** Modes,
    _Out_ UINT* Count)
{
    HRESULT hr;

    if (Adapter->Modes == nullptr)
    {
        hr = D9BuildModeTable(Adapter, IsEx);
        if (FAILED(hr))
            return hr;
    }

    *Modes = Adapter->Modes;
    *Count = Adapter->ModeCount + Adapter->ExtendedModeCount;
    if (!IsEx)
    {
        BOOLEAN Extended = Format == D3DFMT_A2R10G10B10;

        *Modes = Extended ? Adapter->Modes + Adapter->ModeCount : Adapter->Modes;
        *Count = Extended ? Adapter->ExtendedModeCount : Adapter->ModeCount;
    }
    return S_OK;
}

/* The plain runtime lists a mode once even when GDI repeats it with other timings */
static
BOOLEAN
D9IsRepeatedMode(
    _In_reads_(Index) const D3DDISPLAYMODEEX* Modes,
    _In_ UINT Index)
{
    for (UINT Earlier = 0; Earlier < Index; Earlier++)
    {
        if (Modes[Earlier].Width == Modes[Index].Width && Modes[Earlier].Height == Modes[Index].Height &&
            Modes[Earlier].RefreshRate == Modes[Index].RefreshRate && Modes[Earlier].Format == Modes[Index].Format)
        {
            return TRUE;
        }
    }
    return FALSE;
}

STDMETHODIMP_(UINT)
D9Enum::GetAdapterModeCountEx(
    _In_ UINT Adapter,
    _In_ const D3DDISPLAYMODEFILTER* Filter)
{
    D9LockScope Scope(&Lock);
    const D3DDISPLAYMODEEX* Modes;
    UINT Count;
    UINT Listed = 0;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"GetAdapterModeCountEx: adapter %u does not exist", Adapter);
        return 0;
    }
    if (!D9IsFilterValid(Filter))
    {
        D9_TRACE_ONCE(L"GetAdapterModeCountEx: invalid filter");
        return 0;
    }

    /* The extended runtime reads the modes again on every count */
    if (IsEx)
    {
        if (GetAdapterMonitor(Adapter) == NULL)
        {
            D9_TRACE_ONCE(L"GetAdapterModeCountEx: adapter %u is not part of the desktop", Adapter);
            return 0;
        }
        D9FreeModeTable(&Adapters[Adapter]);
    }

    if (!D9IsDesktopFormat(Filter->Format, IsEx) ||
        FAILED(D9ModeRange(&Adapters[Adapter], IsEx, Filter->Format, &Modes, &Count)))
    {
        return 0;
    }

    for (UINT Index = 0; Index < Count; Index++)
    {
        if (D9ModeMatches(Filter, &Modes[Index]) && (IsEx || !D9IsRepeatedMode(Modes, Index)))
            Listed += 1;
    }
    return Listed;
}

HRESULT
D9Enum::FindMode(
    _In_ UINT Adapter,
    _In_ const D3DDISPLAYMODEFILTER* Filter,
    _In_ UINT Mode,
    _Out_ D3DDISPLAYMODEEX* DisplayMode)
{
    const D3DDISPLAYMODEEX* Modes;
    UINT Count;
    UINT Listed = 0;
    HRESULT hr;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"EnumAdapterModes: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }
    if (DisplayMode->Size != sizeof(*DisplayMode))
    {
        D9_TRACE_ONCE(L"EnumAdapterModesEx: invalid mode size %u", DisplayMode->Size);
        return D3DERR_INVALIDCALL;
    }
    if (!D9IsDesktopFormat(Filter->Format, IsEx))
    {
        D9_TRACE_ONCE(L"EnumAdapterModes: no modes in format %u", Filter->Format);
        return D3DERR_INVALIDCALL;
    }

    hr = D9ModeRange(&Adapters[Adapter], IsEx, Filter->Format, &Modes, &Count);
    if (FAILED(hr))
        return hr;

    for (UINT Index = 0; Index < Count; Index++)
    {
        if (!D9ModeMatches(Filter, &Modes[Index]) || (!IsEx && D9IsRepeatedMode(Modes, Index)))
            continue;

        Listed += 1;
        if (Listed - 1 == Mode)
        {
            *DisplayMode = Modes[Index];
            break;
        }
    }

    if (Listed == 0)
    {
        D9_TRACE_ONCE(L"EnumAdapterModes: no modes in format %u", Filter->Format);
        return D3DERR_INVALIDCALL;
    }
    if (Mode >= Listed)
    {
        D9_TRACE_ONCE(L"EnumAdapterModes: mode %u is past the end", Mode);
        return D3DERR_INVALIDCALL;
    }
    return S_OK;
}

STDMETHODIMP_(UINT)
D9Enum::GetAdapterModeCount(
    _In_ UINT Adapter,
    _In_ D3DFORMAT Format)
{
    D3DDISPLAYMODEFILTER Filter = { sizeof(Filter), Format, D3DSCANLINEORDERING_UNKNOWN };

    return GetAdapterModeCountEx(Adapter, &Filter);
}

STDMETHODIMP
D9Enum::EnumAdapterModes(
    _In_ UINT Adapter,
    _In_ D3DFORMAT Format,
    _In_ UINT Mode,
    _Out_ D3DDISPLAYMODE* DisplayMode)
{
    D9LockScope Scope(&Lock);
    D3DDISPLAYMODEFILTER Filter = { sizeof(Filter), Format, D3DSCANLINEORDERING_UNKNOWN };
    D3DDISPLAYMODEEX Found = {};
    HRESULT hr;

    Found.Size = sizeof(Found);
    hr = FindMode(Adapter, &Filter, Mode, &Found);
    DisplayMode->Width = Found.Width;
    DisplayMode->Height = Found.Height;
    DisplayMode->RefreshRate = Found.RefreshRate;
    DisplayMode->Format = Found.Format;
    return hr;
}

STDMETHODIMP
D9Enum::EnumAdapterModesEx(
    _In_ UINT Adapter,
    _In_ const D3DDISPLAYMODEFILTER* Filter,
    _In_ UINT Mode,
    _Out_ D3DDISPLAYMODEEX* DisplayMode)
{
    D9LockScope Scope(&Lock);

    if (!D9IsFilterValid(Filter))
    {
        D9_TRACE_ONCE(L"EnumAdapterModesEx: invalid filter");
        return D3DERR_INVALIDCALL;
    }
    return FindMode(Adapter, Filter, Mode, DisplayMode);
}

STDMETHODIMP
D9Enum::GetAdapterDisplayMode(
    _In_ UINT Adapter,
    _Out_ D3DDISPLAYMODE* DisplayMode)
{
    D9LockScope Scope(&Lock);
    D3DDISPLAYMODEEX Current = {};
    HRESULT hr;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"GetAdapterDisplayMode: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }

    Current.Size = sizeof(Current);
    hr = D9ReadDesktopMode(Adapters[Adapter].DeviceName, &Current, nullptr);
    DisplayMode->Width = Current.Width;
    DisplayMode->Height = Current.Height;
    DisplayMode->RefreshRate = Current.RefreshRate;
    DisplayMode->Format = Current.Format;
    return hr;
}

STDMETHODIMP
D9Enum::GetAdapterDisplayModeEx(
    _In_ UINT Adapter,
    _Out_opt_ D3DDISPLAYMODEEX* DisplayMode,
    _Out_opt_ D3DDISPLAYROTATION* Rotation)
{
    D9LockScope Scope(&Lock);
    D3DDDI_ROTATION Current = static_cast<D3DDDI_ROTATION>(0);
    HRESULT hr;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"GetAdapterDisplayModeEx: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }
    if (DisplayMode->Size != sizeof(*DisplayMode))
    {
        D9_TRACE_ONCE(L"GetAdapterDisplayModeEx: invalid mode size %u", DisplayMode->Size);
        return D3DERR_INVALIDCALL;
    }

    hr = D9ReadDesktopMode(Adapters[Adapter].DeviceName, DisplayMode, &Current);
    if (Rotation != nullptr)
        *Rotation = static_cast<D3DDISPLAYROTATION>(Current);
    return hr;
}
