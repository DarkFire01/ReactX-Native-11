/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Objects shared by every part of the DirectDraw runtime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "rxcommon.hpp"

#include <ddraw.h>
#include <ddrawi.h>
#include <d3d9types.h>
#include <d3dumddi.h>

#include "kmcallbacks.hpp"
#include "trace.hpp"

struct DdDriver;
struct DdLocal;
struct DdInterface;
struct DdClipper;
struct DdPalette;
struct DdSurface;
struct DdResource;

/* Which interface an object pointer stands for */
enum class DdKind : UCHAR
{
    Draw1,
    Draw2,
    Draw4,
    Draw7,
    VideoPortContainer,
};

/* Registry switches read when an object is created */
enum : ULONG
{
    DdRegModeXOnly = 0x00000001,
    DdRegEmulationOnly = 0x00000002,
    DdRegShowFrameRate = 0x00000004,
    DdRegEnablePrintScreen = 0x00000008,
    DdRegForceAgp = 0x00000010,
    DdRegDisableMmx = 0x00000020,
    DdRegDisableWiderSurfaces = 0x00000040,
    DdRegDisableCapsInDesc = 0x00000400,
    DdRegDisableAgp = 0x00000800,
    DdRegForceRefreshRate = 0x00008000,
    DdRegEnumAttachedSecondaries = 0x00008000,
    DdRegUseNonLocalVidMem = 0x00010000,
    DdRegFlipNoVsync = 0x00020000,
};

/* Driver object flags; the low bits follow the public DDRAWI_ values */
enum : ULONG
{
    DdDriverEmulatedCaps = 0x00000400,
    DdDriverHelCaps = 0x00004000,
    DdDriverDoesGdi = 0x00800000,
    DdDriverAttached = 0x01000000,
};

/* Creation flags passed down from the entry points */
enum : ULONG
{
    DdCreateHardwareOnly = 0x00000001,
    DdCreateEmulationOnly = 0x00000002,
};

/* The kernel adapter, kernel device and Direct3D 9 user mode driver device behind a display */
struct DdHardware : public RxKernelDevice
{
    D3DKMT_HANDLE AdapterHandle = 0;
    LUID AdapterLuid = {};
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId = 0;
    HMODULE Module = NULL;
    D3DDDI_ADAPTERCALLBACKS Callbacks = {};
    D3DDDI_ADAPTERFUNCS Functions = {};
    HANDLE DriverAdapter = NULL;
    UINT RuntimeVersion = 0;
    UINT DriverVersion = 0;
    BOOLEAN IsOldDisplayModel = FALSE;

    /* The device every surface of this display is created on */
    D3DKMT_HANDLE DeviceHandle = 0;
    D3DKMT_HANDLE ContextHandle = 0;
    D3DDDI_DEVICECALLBACKS DeviceCallbacks = {};
    D3DDDI_DEVICEFUNCS DeviceFunctions = {};
    HANDLE DriverDevice = NULL;
    BOOLEAN UsesFlush1 = FALSE;
    BOOLEAN Lost = FALSE;

    /* The present the runtime is building while the driver runs it; DdPresentCb finishes it */
    D3DKMT_PRESENT Present = {};

    /* The window redirection surface DWM shared last, opened on this device */
    DdResource* WindowSurface = nullptr;
    D3DKMT_HANDLE WindowShare = 0;

    D3DKMT_HANDLE
    KernelDevice() const override;

    D3DKMT_HANDLE
    KernelAdapter() const override;

    D3DKMT_CLIENTHINT
    KernelClientHint() const override;

    D3DKMT_HANDLE
    KernelResourceOf(
        _In_opt_ HANDLE RuntimeResource) override;

    VOID
    RetireKernelResource(
        _In_ HANDLE RuntimeResource) override;

    /* Memory as the kernel reports it, after the runtime's clamps */
    ULONGLONG LocalMemory = 0;
    ULONGLONG NonLocalMemory = 0;
    ULONGLONG LocalMemoryFree = 0;
    ULONGLONG NonLocalMemoryFree = 0;

    D3DKMT_FLIPQUEUEINFO FlipQueue = {};
    D3DKMT_ADAPTERTYPE AdapterType = {};
    D3DKMT_WDDM_1_2_CAPS Wddm12Caps = {};

    FORMATOP* Formats = nullptr;
    UINT FormatCount = 0;
    BOOLEAN FormatsQueried = FALSE;

    WCHAR UmdName[MAX_PATH] = {};
};

/* One display mode as the driver object remembers it */
struct DdModeInfo
{
    DWORD Width;
    DWORD Height;
    LONG Pitch;
    DWORD BitCount;
    WORD Flags;
    WORD RefreshRate;
    DWORD RedMask;
    DWORD GreenMask;
    DWORD BlueMask;
    DWORD AlphaMask;
};

/* The short mode signature TestCooperativeLevel compares */
struct DdModeKey
{
    WORD Width;
    WORD Height;
    BYTE BitCount;
    BYTE MonitorCount;
    WORD RefreshRate;
};

/* Per display state shared by every DirectDraw object that uses it */
struct DdDriver
{
    LONG RefCount;
    ULONG Flags;
    CHAR Name[32];
    CHAR DeviceName[32];
    DdDriver* Next;

    DdHardware* Hardware;

    DDCORECAPS HalCaps;
    DDNONLOCALVIDMEMCAPS HalNonLocalCaps;
    BOOLEAN HasNonLocalCaps;
    DDSCAPSEX HalMoreCaps;

    DDCORECAPS HelCaps;
    DDNONLOCALVIDMEMCAPS HelNonLocalCaps;
    DDSCAPSEX HelMoreCaps;
    DWORD Internal1;

    DWORD MonitorFrequency;
    DWORD* FourCC;
    DWORD FourCCCount;

    DdModeInfo Mode;
    DdModeKey ModeKey;
    RECT DeviceRect;
    RECT DesktopRect;
    DWORD MonitorCount;

    DdLocal* ExclusiveOwner;
    ULONG FakeFlags;

    /* The adapter an emulated driver reports memory for */
    LUID AdapterLuid;

    DdClipper* Clippers;
    DdPalette* Palettes;
    DdSurface* Surfaces;
    DdSurface* Primary;
};

/* One DirectDraw object as the application created it */
struct DdLocal
{
    DdDriver* Driver;
    ULONG Flags;
    LONG RefCount;
    DWORD ProcessId;
    HWND Window;
    HWND FocusWindow;
    HDC Dc;
    DWORD AppHackFlags;
    DdModeKey SavedMode;
    BOOLEAN MaxWindowedMode;
    DdLocal* Next;
};

/* One interface pointer handed to the application */
struct DdInterface
{
    const VOID* Vtbl;
    DdLocal* Local;
    DdInterface* Next;
    ULONG RefCount;
    DdKind Kind;
    BOOLEAN Initialized;
};

/* An IDirectDrawClipper; clippers have a single interface */
struct DdClipper
{
    const VOID* Vtbl;
    ULONG RefCount;
    BOOLEAN Initialized;
    DWORD ProcessId;
    DdDriver* Driver;
    DdLocal* Creator;
    DdInterface* Owner;
    BOOLEAN MadeByFirstDraw;
    HWND Window;
    RGNDATA* ClipList;
    DdClipper* Next;
};

/* An IDirectDrawPalette */
struct DdPalette
{
    const VOID* Vtbl;
    ULONG RefCount;
    ULONG Flags;
    DWORD ProcessId;
    BOOLEAN OnPrimary;
    DdDriver* Driver;
    DdLocal* Creator;
    DdInterface* Owner;
    BYTE* Entries;
    DdPalette* Next;
};

/* One interface pointer of a surface; each interface version keeps its own count */
struct DdSurfaceInterface
{
    const VOID* Vtbl;
    DdSurface* Surface;
    ULONG RefCount;
    UCHAR Version;
    DdSurfaceInterface* Next;
};

/* A user mode driver resource behind a video memory surface */
struct DdResource
{
    HANDLE DriverResource;
    D3DKMT_HANDLE KernelResource;
    D3DDDIFORMAT Format;
    D3DDDI_POOL Pool;
    D3DDDI_RESOURCEFLAGS Flags;
    UINT SurfaceCount;
    UINT MipLevels;
    UINT Charged;
    BOOLEAN ChargedShared;
    USHORT LockCount;
};

/* One SetPrivateData entry; small copies or an IUnknown the surface holds a reference on */
struct DdPrivateData
{
    GUID Tag;
    DWORD Size;
    DWORD Flags;
    DWORD Stamp;
    union
    {
        BYTE* Bytes;
        IUnknown* Object;
    };
    DdPrivateData* Next;
};

/* How a surface came to be part of a complex structure, and what it was there */
enum : ULONG
{
    DdChainImplicit = 0x00000001,
    DdChainRoot = 0x00000002,
    DdChainWasFront = 0x00000004,
    DdChainWasBack = 0x00000008,
    DdChainDying = 0x00000010,
};

/* One attachment, threaded on the attaching surface's out list and the attached surface's in list */
struct DdLink
{
    DdSurface* From;
    DdSurface* To;
    UCHAR FromVersion;
    UCHAR ToVersion;
    BOOLEAN Implicit;
    DdLink* NextOut;
    DdLink* NextIn;
};

/* A DirectDraw surface; video memory surfaces keep their pixels in a driver resource */
struct DdSurface
{
    ULONG RefCount;
    DdDriver* Driver;
    DdLocal* Creator;
    DdInterface* Owner;
    DdKind MakerKind;
    DWORD ProcessId;
    DDSCAPS2 Caps;
    DDPIXELFORMAT Format;
    DWORD Width;
    DWORD Height;
    LONG Pitch;
    BYTE* Bits;
    HANDLE Section;
    DdResource* Resource;
    DdResource* SystemResource;
    BOOLEAN DriverPending;
    DWORD KeyFlags;
    DDCOLORKEY SourceKey;
    DDCOLORKEY DestKey;
    DDCOLORKEY SourceOverlayKey;
    DDCOLORKEY DestOverlayKey;
    BOOLEAN Locked;
    BOOLEAN LockedReadOnly;
    RECT LockRect;
    HDC Dc;
    HBITMAP Bitmap;
    HGDIOBJ OldBitmap;
    DdPalette* Palette;
    DdClipper* Clipper;
    DdSurface* FlipNext;
    DdSurface* Next;
    DWORD Uniqueness;
    DWORD Priority;
    DWORD Lod;
    DdSurfaceInterface* Interfaces;
    DdPrivateData* PrivateData;
    DdLink* Attached;
    DdLink* AttachedFrom;
    ULONG ChainFlags;
    DWORD BackBufferCount;

    /* Lost surfaces keep their identity until Restore gives them memory again */
    BOOLEAN Lost;
    BOOLEAN SysMemRequested;
    BOOLEAN ClientMemory;
    DWORD FillBitCount;
    DdModeKey CreatedMode;
};

/* Process wide runtime state */
struct DdGlobals
{
    CRITICAL_SECTION Lock;
    LONG Recursion;
    UINT DisplayUniqueness;
    BOOLEAN DeviceLost;
    BOOLEAN RestoreFailed;
    BOOLEAN Restoring;
    HMODULE Module;

    DdInterface* Interfaces;
    DdLocal* Locals;
    DdDriver* Drivers;
    VOID* Clippers;
    LONG LockCount;

    ULONG RegFlags;
    DWORD ForcedRefreshRate;
    BOOLEAN ReloadRegistry;
    CHAR PrimaryDisplay[32];

    /* SetAppCompatData switches */
    BOOLEAN EnableOverlays;
    BOOLEAN DisableMaxWindowedMode;
    BOOLEAN DwmOffForPrimaryLock;
    BOOLEAN DwmOffForPrimaryBlt;
    BOOLEAN ForceFullscreenSprite;
    BOOLEAN ForceBltToPrimary;
    BOOLEAN DisableSurfaceLock;
    BOOLEAN RedirectPrimaryBlts;
    BOOLEAN StripMaxWindowBorder;
    DWORD ShadowBufferColor;

    DWORD BlankToggle;
};

extern DdGlobals DdState;

/* The answer for optimized surfaces; the SDK headers have no name for this code */
static constexpr HRESULT DdErrOptimizedSurface = MAKE_DDHRESULT(655);

/* Takes the runtime lock and resets objects when the display changed underneath them */
class DdLockScope
{
public:
    DdLockScope();
    ~DdLockScope();

    DdLockScope(const DdLockScope&) = delete;
    DdLockScope& operator=(const DdLockScope&) = delete;
};

VOID
DdEnterLock(VOID);

VOID
DdLeaveLock(VOID);

/* globals.cpp */
UINT
DdQueryDisplayUniqueness(VOID);

VOID
DdRefreshPrimaryName(VOID);

BOOLEAN
DdIsMultiMonitor(VOID);

BOOLEAN
DdEnumDisplayDevice(
    _In_ DWORD Index,
    _Out_ DISPLAY_DEVICEA* Device);

_Ret_maybenull_
HDC
DdCreateDisplayDc(
    _In_z_ PCSTR Name);

VOID
DdReadRegistry(VOID);

_Ret_maybenull_
HMONITOR
DdMonitorFromDevice(
    _In_z_ PCSTR DeviceName);

/* hardware.cpp */
_Ret_maybenull_
DdHardware*
DdOpenHardware(
    _In_ HDC Dc);

VOID
DdCloseHardware(
    _In_opt_ DdHardware* Hardware);

HRESULT
DdHardwareGetCaps(
    _In_ DdHardware* Hardware,
    _In_ D3DDDICAPS_TYPE Type,
    _In_opt_ VOID* Info,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ UINT Size);

BOOLEAN
DdHardwareQueryFormats(
    _Inout_ DdHardware* Hardware);

HRESULT
DdHresultFromStatus(
    _In_ NTSTATUS Status);

BOOLEAN
DdReadAdapterIdentity(
    _In_z_ PCSTR DeviceName,
    _Out_ DWORD* VendorId,
    _Out_ DWORD* DeviceId,
    _Out_ DWORD* SubSysId,
    _Out_ DWORD* Revision,
    _Out_writes_z_(DriverSize) PSTR Driver,
    _In_ SIZE_T DriverSize);

BOOLEAN
DdAdapterLuidFromDc(
    _In_ HDC Dc,
    _Out_ LUID* AdapterLuid);

HRESULT
DdQueryEmulatedMemory(
    _In_ const LUID& AdapterLuid,
    _In_ DWORD Caps,
    _Out_ DWORD* Total,
    _Out_ DWORD* Free);

/* device.cpp */
BOOLEAN
DdCreateKernelDevice(
    _Inout_ DdHardware* Hardware);

BOOLEAN
DdCreateDriverDevice(
    _Inout_ DdHardware* Hardware);

VOID
DdDestroyDriverDevice(
    _Inout_ DdHardware* Hardware);

VOID
DdDestroyKernelDevice(
    _Inout_ DdHardware* Hardware);

D3DDDIFORMAT
DdDriverFormat(
    _In_ const DDPIXELFORMAT* Format);

BOOLEAN
DdDriverCanCreate(
    _In_ DdHardware* Hardware,
    _In_ const DDSCAPS2* Caps,
    _In_ const DDPIXELFORMAT* Format);

HRESULT
DdCreateSurfaceResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface);

VOID
DdDestroySurfaceResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface);

HRESULT
DdLockSurfaceResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface,
    _In_opt_ const RECT* Rect,
    _In_ DWORD Flags,
    _Outptr_ BYTE** Bits);

HRESULT
DdUnlockSurfaceResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface);

HRESULT
DdCreateStagingResource(
    _In_ DdHardware* Hardware,
    _In_ D3DDDIFORMAT Format,
    _In_ UINT Width,
    _In_ UINT Height,
    _Out_ DdResource* Resource);

HRESULT
DdCreateSystemResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface);

VOID
DdDestroySystemResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface);

VOID
DdWaitForDriver(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface);

VOID
DdMarkDeviceLost(
    _Inout_ DdHardware* Hardware);

/* present.cpp */
HRESULT
APIENTRY
DdPresentCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_PRESENT* Args);

HRESULT
APIENTRY
DdSubmitPresentToHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SUBMITPRESENTTOHWQUEUE* Args);

VOID
DdReleaseWindowSurface(
    _Inout_ DdHardware* Hardware);

HRESULT
DdPresentThroughDwm(
    _In_ DdSurface* Primary,
    _In_ HWND Window,
    _In_ const RECT* To,
    _In_opt_ DdSurface* Source,
    _In_opt_ const RECT* From,
    _In_ BOOLEAN Fill,
    _In_ DWORD FillValue,
    _Out_ BOOLEAN* Handled);

/* caps.cpp */
BOOLEAN
DdBuildHardwareCaps(
    _Inout_ DdDriver* Driver);

VOID
DdBuildEmulatedCaps(
    _Inout_ DdDriver* Driver);

VOID
DdBuildHelCaps(
    _Inout_ DdDriver* Driver);

BOOLEAN
DdReadDisplayMode(
    _Inout_ DdDriver* Driver);

VOID
DdUpdateDeviceRect(
    _Inout_ DdDriver* Driver);

VOID
DdFillPixelFormat(
    _Out_ DDPIXELFORMAT* Format,
    _In_opt_ HDC Dc,
    _In_ DWORD BitCount);

/* driver.cpp */
HRESULT
DdCreateObject(
    _In_opt_ GUID* Guid,
    _Out_ DdInterface** Interface,
    _In_opt_ DdInterface* Existing,
    _In_ ULONG LocalFlags);

_Ret_maybenull_
DdInterface*
DdFindInterface(
    _In_ DdLocal* Local,
    _In_ DdKind Kind,
    _In_ BOOLEAN Initialized);

_Ret_maybenull_
DdInterface*
DdNewUninitializedObject(
    _In_ DdKind Kind);

VOID
DdFreeUninitializedObject(
    _In_ DdInterface* Interface);

VOID
DdResetAllObjects(
    _In_ BOOLEAN Restore);

ULONG
DdReleaseInterface(
    _In_ DdInterface* Interface);

ULONG
DdAddRefInterface(
    _In_ DdInterface* Interface);

/* clipper.cpp */
HRESULT
DdNewClipper(
    _In_opt_ DdDriver* Driver,
    _In_ DWORD Flags,
    _Out_ LPDIRECTDRAWCLIPPER* Result,
    _In_opt_ IUnknown* Outer,
    _In_ BOOLEAN Initialized,
    _In_opt_ DdInterface* Owner);

ULONG
DdReleaseClipper(
    _In_ DdClipper* Clipper);

VOID
DdReleaseLocalClippers(
    _In_ DdLocal* Local);

VOID
DdOrphanClippers(
    _In_ DdDriver* Driver);

/* palette.cpp */
HRESULT
DdNewPalette(
    _In_ DdInterface* This,
    _In_ DWORD Flags,
    _In_ LPPALETTEENTRY Initial,
    _Out_ LPDIRECTDRAWPALETTE* Result,
    _In_opt_ IUnknown* Outer);

ULONG
DdReleasePalette(
    _In_ DdPalette* Palette);

VOID
DdReleaseLocalPalettes(
    _In_ DdLocal* Local);

/* surface.cpp */
HRESULT
DdCreateSurfaceObject(
    _In_ DdInterface* This,
    _In_ const DDSURFACEDESC2* Desc,
    _Out_ VOID** Result,
    _In_opt_ IUnknown* Outer);

VOID
DdReleaseLocalSurfaces(
    _In_ DdLocal* Local);

_Ret_maybenull_
DdSurface*
DdSurfaceFromCom(
    _In_opt_ VOID* Com);

BOOLEAN
DdSurfaceBusy(
    _In_ const DdSurface* Surface);

HRESULT
DdMapSurface(
    _Inout_ DdSurface* Surface,
    _In_ BOOLEAN ReadOnly,
    _Outptr_ BYTE** Bits);

VOID
DdUnmapSurface(
    _Inout_ DdSurface* Surface);

VOID
DdTouchSurface(
    _Inout_ DdSurface* Surface);

VOID
DdHoldSurface(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version);

VOID
DdDropSurface(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version);

ULONG
DdSurfaceRefsOn(
    _In_ const DdSurface* Surface,
    _In_ UCHAR Version);

_Ret_maybenull_
VOID*
DdHandOutSurface(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version);

UCHAR
DdSurfaceVersionOf(
    _In_ VOID* Com);

VOID
DdDescribeSurface(
    _In_ DdSurface* Surface,
    _Out_ DDSURFACEDESC2* Desc);

VOID
DdInvalidateSurfaces(
    _Inout_ DdDriver* Driver);

HRESULT
DdRestoreLocalSurfaces(
    _In_ DdLocal* Local);

/* attach.cpp */
HRESULT
DdAddLink(
    _In_ DdSurface* From,
    _In_ UCHAR FromVersion,
    _In_ DdSurface* To,
    _In_ UCHAR ToVersion,
    _In_ BOOLEAN Implicit);

VOID
DdDetachForDestroy(
    _Inout_ DdSurface* Surface,
    _In_ UCHAR Version);

HRESULT
STDMETHODCALLTYPE
DdSurfaceAddAttached(
    _In_ VOID* Com,
    _In_opt_ VOID* Attach);

HRESULT
STDMETHODCALLTYPE
DdSurfaceDeleteAttached(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ VOID* Attach);

HRESULT
STDMETHODCALLTYPE
DdSurfaceEnumAttached(
    _In_ VOID* Com,
    _In_opt_ VOID* Context,
    _In_opt_ VOID* Callback);

HRESULT
STDMETHODCALLTYPE
DdSurfaceGetAttached(
    _In_ VOID* Com,
    _In_opt_ VOID* Caps,
    _Out_opt_ VOID** Result);

HRESULT
STDMETHODCALLTYPE
DdSurfaceFlip(
    _In_ VOID* Com,
    _In_opt_ VOID* Target,
    _In_ DWORD Flags);

HRESULT
STDMETHODCALLTYPE
DdSurfaceGetFlipStatus(
    _In_ VOID* Com,
    _In_ DWORD Flags);

/* overlay.cpp */
HRESULT
STDMETHODCALLTYPE
DdSurfaceUpdateOverlay(
    _In_ VOID* Com,
    _In_opt_ LPRECT SourceRect,
    _In_opt_ VOID* Target,
    _In_opt_ LPRECT TargetRect,
    _In_ DWORD Flags,
    _In_opt_ LPDDOVERLAYFX Fx);

HRESULT
STDMETHODCALLTYPE
DdSurfaceSetOverlayPosition(
    _In_ VOID* Com,
    _In_ LONG X,
    _In_ LONG Y);

HRESULT
STDMETHODCALLTYPE
DdSurfaceGetOverlayPosition(
    _In_ VOID* Com,
    _Out_opt_ LPLONG X,
    _Out_opt_ LPLONG Y);

HRESULT
STDMETHODCALLTYPE
DdSurfaceEnumOverlayZOrders(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ VOID* Context,
    _In_opt_ VOID* Callback);

HRESULT
STDMETHODCALLTYPE
DdSurfaceUpdateOverlayZOrder(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ VOID* Reference);

HRESULT
STDMETHODCALLTYPE
DdSurfaceAddOverlayDirtyRect(
    _In_ VOID* Com,
    _In_opt_ LPRECT Rect);

HRESULT
STDMETHODCALLTYPE
DdSurfaceUpdateOverlayDisplay(
    _In_ VOID* Com,
    _In_ DWORD Flags);

/* blit.cpp */
HRESULT
DdBlt(
    _In_ DdSurface* Target,
    _In_opt_ const RECT* TargetRect,
    _In_opt_ DdSurface* Source,
    _In_opt_ const RECT* SourceRect,
    _In_ DWORD Flags,
    _In_opt_ const DDBLTFX* Fx);

HRESULT
DdBltFast(
    _In_ DdSurface* Target,
    _In_ DWORD X,
    _In_ DWORD Y,
    _In_ DdSurface* Source,
    _In_opt_ const RECT* SourceRect,
    _In_ DWORD Flags);

/* object.cpp */
const VOID*
DdVtblFor(
    _In_ DdKind Kind,
    _In_ BOOLEAN Initialized);

HRESULT
DdGetAvailableMemory(
    _In_ DdLocal* Local,
    _In_ const DDSCAPS2* Caps,
    _Out_opt_ DWORD* Total,
    _Out_opt_ DWORD* Free);

HRESULT
DdQueryObject(
    _In_ DdInterface* This,
    _In_opt_ const IID* Iid,
    _Outptr_ VOID** Object);

/* Cast helpers between the application pointer and our interface record */
static
inline
DdInterface*
DdFromCom(
    _In_ VOID* Com)
{
    return static_cast<DdInterface*>(Com);
}

static
inline
BOOLEAN
DdIsOldDraw(
    _In_ const DdInterface* This)
{
    return This->Kind == DdKind::Draw1 || This->Kind == DdKind::Draw2;
}
