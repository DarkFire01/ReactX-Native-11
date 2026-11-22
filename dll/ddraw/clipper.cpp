/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDirectDrawClipper objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

extern const VOID* const DdClipperVtbl[];
extern const VOID* const DdUninitClipperVtbl[];

static
VOID
DdLinkClipper(
    _In_ DdClipper* Clipper,
    _In_opt_ DdDriver* Driver)
{
    DdClipper** Head = Driver != nullptr ? &Driver->Clippers : reinterpret_cast<DdClipper**>(&DdState.Clippers);

    Clipper->Driver = Driver;
    Clipper->Next = *Head;
    *Head = Clipper;
}

static
VOID
DdUnlinkClipper(
    _In_ DdClipper* Clipper)
{
    DdClipper** Link;

    Link = Clipper->Driver != nullptr ? &Clipper->Driver->Clippers : reinterpret_cast<DdClipper**>(&DdState.Clippers);
    for (; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Clipper)
        {
            *Link = Clipper->Next;
            return;
        }
    }
}

/* Clippers a DirectDraw object made die with it */
VOID
DdReleaseLocalClippers(
    _In_ DdLocal* Local)
{
    DdClipper* Clipper;
    DdClipper* Next;

    for (Clipper = Local->Driver->Clippers; Clipper != nullptr; Clipper = Next)
    {
        Next = Clipper->Next;
        if (Clipper->Creator == Local && Clipper->ProcessId == Local->ProcessId)
        {
            Clipper->Creator = nullptr;
            Clipper->RefCount = 1;
            DdReleaseClipper(Clipper);
        }
    }
}

/* Clippers still alive when their display goes away move to the global list */
VOID
DdOrphanClippers(
    _In_ DdDriver* Driver)
{
    DdClipper* Clipper;

    while ((Clipper = Driver->Clippers) != nullptr)
    {
        Driver->Clippers = Clipper->Next;
        DdLinkClipper(Clipper, nullptr);
    }
}

HRESULT
DdNewClipper(
    _In_opt_ DdDriver* Driver,
    _In_ DWORD Flags,
    _Out_ LPDIRECTDRAWCLIPPER* Result,
    _In_opt_ IUnknown* Outer,
    _In_ BOOLEAN Initialized,
    _In_opt_ DdInterface* Owner)
{
    DdClipper* Clipper;

    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    if (Result == nullptr)
        return E_INVALIDARG;
    *Result = nullptr;
    if (Flags != 0)
        return E_INVALIDARG;

    Clipper = new (std::nothrow) DdClipper;
    if (Clipper == nullptr)
        return E_OUTOFMEMORY;

    ZeroMemory(Clipper, sizeof(*Clipper));
    Clipper->Vtbl = Initialized ? DdClipperVtbl : DdUninitClipperVtbl;
    Clipper->Initialized = Initialized;
    Clipper->RefCount = 1;
    Clipper->ProcessId = GetCurrentProcessId();
    Clipper->Creator = Owner != nullptr ? Owner->Local : nullptr;
    Clipper->MadeByFirstDraw = Owner != nullptr && Owner->Kind == DdKind::Draw1;
    DdLinkClipper(Clipper, Driver);

    /* Newer DirectDraw interfaces stay alive as long as their clippers */
    if (Owner != nullptr && !DdIsOldDraw(Owner))
    {
        DdAddRefInterface(Owner);
        Clipper->Owner = Owner;
    }

    *Result = reinterpret_cast<LPDIRECTDRAWCLIPPER>(Clipper);
    return S_OK;
}

static
DdClipper*
DdClipperFromCom(
    _In_ VOID* Com)
{
    return static_cast<DdClipper*>(Com);
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperQueryInterface(
    _In_ VOID* Com,
    _In_opt_ const IID* Iid,
    _Outptr_ VOID** Object)
{
    DdClipper* Clipper = DdClipperFromCom(Com);

    DdLockScope Lock;

    if (Iid == nullptr || Object == nullptr)
        return E_INVALIDARG;
    *Object = nullptr;

    if (!IsEqualGUID(*Iid, IID_IUnknown) && !IsEqualGUID(*Iid, IID_IDirectDrawClipper))
        return E_NOINTERFACE;

    Clipper->RefCount += 1;
    *Object = Clipper;
    return S_OK;
}

static
ULONG
STDMETHODCALLTYPE
DdClipperAddRef(
    _In_ VOID* Com)
{
    DdClipper* Clipper = DdClipperFromCom(Com);

    DdLockScope Lock;

    Clipper->RefCount += 1;
    return Clipper->RefCount;
}

ULONG
DdReleaseClipper(
    _In_ DdClipper* Clipper)
{
    DdInterface* Owner;
    ULONG Remaining;

    Clipper->RefCount -= 1;
    Remaining = Clipper->RefCount;
    if (Remaining != 0)
        return Remaining;

    Owner = Clipper->Owner;
    DdUnlinkClipper(Clipper);
    LocalFree(Clipper->ClipList);
    Clipper->Vtbl = nullptr;
    delete Clipper;

    if (Owner != nullptr)
        DdReleaseInterface(Owner);
    return 0;
}

static
ULONG
STDMETHODCALLTYPE
DdClipperRelease(
    _In_ VOID* Com)
{
    DdClipper* Clipper = DdClipperFromCom(Com);

    DdLockScope Lock;

    if (Clipper == nullptr || (Clipper->Vtbl != DdClipperVtbl && Clipper->Vtbl != DdUninitClipperVtbl))
        return 0;
    return DdReleaseClipper(Clipper);
}

/* Trims every rectangle of a region to a bound, dropping the ones left empty */
static
VOID
DdClipRegion(
    _In_opt_ const RECT* Bound,
    _Inout_opt_ RGNDATA* Region)
{
    RECT* Source;
    RECT* Target;
    DWORD Count;
    DWORD Index;
    RECT Cut;

    if (Bound == nullptr || Region == nullptr)
        return;

    /* Nothing to do when the bound already holds the whole region */
    if (Bound->top <= Region->rdh.rcBound.top && Bound->bottom >= Region->rdh.rcBound.bottom &&
        Bound->left <= Region->rdh.rcBound.left && Bound->right >= Region->rdh.rcBound.right)
    {
        return;
    }

    Source = reinterpret_cast<RECT*>(Region->Buffer);
    Target = Source;
    Count = Region->rdh.nCount;
    for (Index = 0; Index < Count; Index++)
    {
        Cut.left = Source[Index].left <= Bound->left ? Bound->left : Source[Index].left;
        Cut.right = Source[Index].right >= Bound->right ? Bound->right : Source[Index].right;
        Cut.top = Source[Index].top <= Bound->top ? Bound->top : Source[Index].top;
        Cut.bottom = Source[Index].bottom >= Bound->bottom ? Bound->bottom : Source[Index].bottom;

        if (Cut.bottom - Cut.top <= 0 || Cut.right - Cut.left <= 0)
            Region->rdh.nCount -= 1;
        else
            *Target++ = Cut;
    }
}

static
HRESULT
DdWindowClipList(
    _In_ HWND Window,
    _In_opt_ const RECT* Bound,
    _Out_writes_bytes_opt_(*Size) RGNDATA* Region,
    _Inout_ DWORD* Size)
{
    WINDOWINFO Info = {};
    DWORD Wanted;
    DWORD Have;
    HRGN Visible;
    HDC Dc;

    Dc = GetDC(Window);
    if (Dc == NULL)
        return E_FAIL;

    Visible = CreateRectRgn(0, 0, 0, 0);
    if (Visible == NULL || GetRandomRgn(Dc, Visible, SYSRGN) == -1)
    {
        ReleaseDC(Window, Dc);
        if (Visible != NULL)
            DeleteObject(Visible);
        return E_FAIL;
    }

    if (Region == nullptr)
    {
        *Size = GetRegionData(Visible, 0, NULL);
        ReleaseDC(Window, Dc);
        DeleteObject(Visible);
        return *Size != 0 ? S_OK : E_FAIL;
    }

    Have = *Size;
    Wanted = GetRegionData(Visible, Have, Region);
    if (Wanted == 0)
    {
        Wanted = GetRegionData(Visible, 0, NULL);
        if (Have < Wanted)
        {
            ReleaseDC(Window, Dc);
            DeleteObject(Visible);
            *Size = Wanted;
            return DDERR_REGIONTOOSMALL;
        }
    }

    ReleaseDC(Window, Dc);
    DeleteObject(Visible);
    if (Wanted == 0)
        return E_FAIL;

    /* The visible region is trimmed to the client area, then to what the caller asked about */
    Info.cbSize = sizeof(Info);
    GetWindowInfo(Window, &Info);
    DdClipRegion(&Info.rcClient, Region);
    DdClipRegion(Bound, Region);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperGetClipList(
    _In_ VOID* Com,
    _In_opt_ LPRECT Bound,
    _Out_writes_bytes_opt_(*Size) LPRGNDATA Region,
    _Inout_ LPDWORD Size)
{
    DdClipper* Clipper = DdClipperFromCom(Com);
    DWORD Wanted;

    DdLockScope Lock;

    if (Size == nullptr)
        return E_INVALIDARG;
    if (Region != nullptr)
        Region->rdh.nCount = 0;

    if (Clipper->Window != NULL)
        return DdWindowClipList(Clipper->Window, Bound, Region, Size);

    if (Clipper->ClipList == nullptr)
        return DDERR_NOCLIPLIST;

    Wanted = sizeof(RGNDATAHEADER) + Clipper->ClipList->rdh.nCount * sizeof(RECT);
    if (Region == nullptr || *Size < Wanted)
    {
        *Size = Wanted;
        return Region == nullptr ? S_OK : DDERR_REGIONTOOSMALL;
    }

    memcpy(Region, Clipper->ClipList, Wanted);
    DdClipRegion(Bound, Region);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperGetHWnd(
    _In_ VOID* Com,
    _Out_ HWND* Window)
{
    DdLockScope Lock;

    if (Window == nullptr)
        return E_INVALIDARG;
    *Window = DdClipperFromCom(Com)->Window;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperInitialize(
    _In_ VOID* Com,
    _In_opt_ LPDIRECTDRAW Draw,
    _In_ DWORD Flags)
{
    DdClipper* Clipper = DdClipperFromCom(Com);
    DdInterface* Owner = reinterpret_cast<DdInterface*>(Draw);

    DdLockScope Lock;

    if (Clipper->Initialized)
        return DDERR_ALREADYINITIALIZED;
    if (Flags != 0)
        return E_INVALIDARG;

    if (Owner != nullptr)
    {
        DdUnlinkClipper(Clipper);
        DdLinkClipper(Clipper, Owner->Local->Driver);
        Clipper->Owner = nullptr;
    }

    Clipper->Initialized = TRUE;
    Clipper->Vtbl = DdClipperVtbl;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperIsClipListChanged(
    _In_ VOID* Com,
    _Out_ BOOL* Changed)
{
    DdLockScope Lock;

    if (Changed == nullptr)
        return E_INVALIDARG;

    /* The system runtime always answers that the list may have changed */
    *Changed = TRUE;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperSetClipList(
    _In_ VOID* Com,
    _In_opt_ LPRGNDATA Region,
    _In_ DWORD Flags)
{
    DdClipper* Clipper = DdClipperFromCom(Com);
    RGNDATA* Copy;
    DWORD Size;

    DdLockScope Lock;

    if (Flags != 0)
        return E_INVALIDARG;

    /* The first DirectDraw interface took any header; later ones want a proper rectangle list */
    if (Region != nullptr &&
        (Region->rdh.nCount == 0 ||
         (!Clipper->MadeByFirstDraw &&
          (Region->rdh.dwSize < sizeof(RGNDATAHEADER) || Region->rdh.iType != RDH_RECTANGLES))))
    {
        return DDERR_INVALIDCLIPLIST;
    }

    if (Clipper->Window != NULL)
        return DDERR_CLIPPERISUSINGHWND;

    if (Region == nullptr)
    {
        LocalFree(Clipper->ClipList);
        Clipper->ClipList = nullptr;
        return S_OK;
    }

    Size = sizeof(RGNDATAHEADER) + Region->rdh.nCount * sizeof(RECT);
    Copy = static_cast<RGNDATA*>(LocalAlloc(LPTR, Size));
    if (Copy == nullptr)
        return E_OUTOFMEMORY;

    memcpy(Copy, Region, Size);
    LocalFree(Clipper->ClipList);
    Clipper->ClipList = Copy;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperSetHWnd(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ HWND Window)
{
    DdLockScope Lock;

    if ((Window != NULL && !IsWindow(Window)) || Flags != 0)
        return E_INVALIDARG;
    DdClipperFromCom(Com)->Window = Window;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperUninit1(
    _In_opt_ VOID* Unused0,
    _In_opt_ VOID* Unused1)
{
    return CO_E_NOTINITIALIZED;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperUninit2(
    _In_opt_ VOID* Unused0,
    _In_opt_ VOID* Unused1,
    _In_opt_ VOID* Unused2)
{
    return CO_E_NOTINITIALIZED;
}

static
HRESULT
STDMETHODCALLTYPE
DdClipperUninit3(
    _In_opt_ VOID* Unused0,
    _In_opt_ VOID* Unused1,
    _In_opt_ VOID* Unused2,
    _In_opt_ VOID* Unused3)
{
    return CO_E_NOTINITIALIZED;
}

#define DD_CLIPPER_SLOT(Function) reinterpret_cast<const VOID*>(&Function)

const VOID* const DdClipperVtbl[] =
{
    DD_CLIPPER_SLOT(DdClipperQueryInterface), DD_CLIPPER_SLOT(DdClipperAddRef), DD_CLIPPER_SLOT(DdClipperRelease),
    DD_CLIPPER_SLOT(DdClipperGetClipList), DD_CLIPPER_SLOT(DdClipperGetHWnd), DD_CLIPPER_SLOT(DdClipperInitialize),
    DD_CLIPPER_SLOT(DdClipperIsClipListChanged), DD_CLIPPER_SLOT(DdClipperSetClipList),
    DD_CLIPPER_SLOT(DdClipperSetHWnd),
};

const VOID* const DdUninitClipperVtbl[] =
{
    DD_CLIPPER_SLOT(DdClipperQueryInterface), DD_CLIPPER_SLOT(DdClipperAddRef), DD_CLIPPER_SLOT(DdClipperRelease),
    DD_CLIPPER_SLOT(DdClipperUninit3), DD_CLIPPER_SLOT(DdClipperUninit1), DD_CLIPPER_SLOT(DdClipperInitialize),
    DD_CLIPPER_SLOT(DdClipperUninit1), DD_CLIPPER_SLOT(DdClipperUninit2), DD_CLIPPER_SLOT(DdClipperUninit2),
};
