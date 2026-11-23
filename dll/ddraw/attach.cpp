/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Surface attachments and the flip chains they form
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

/* Caps that keep two surfaces from sharing a flip chain when only one has them */
static constexpr DWORD DdChainKindCaps = DDSCAPS_ZBUFFER | DDSCAPS_TEXTURE | DDSCAPS_OVERLAY | DDSCAPS_ALPHA;

/* A surface may hold one attachment of each of these kinds */
static constexpr DWORD DdOneOfEachCaps = DDSCAPS_MIPMAP | DDSCAPS_TEXTURE | DDSCAPS_ZBUFFER | DDSCAPS_ALPHA;

static constexpr DWORD DdMipTexture = DDSCAPS_MIPMAP | DDSCAPS_TEXTURE;

/* Caps bits no surface may be asked for by */
static constexpr DWORD DdReservedCaps = 0x03000000;

static constexpr DWORD DdChainRoleCaps = DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER;

/* Whether two surfaces are alike enough to flip between */
static
BOOLEAN
DdCanFlipBetween(
    _In_ const DdSurface* First,
    _In_ const DdSurface* Second)
{
    DWORD Mine = First->Caps.dwCaps;
    DWORD Theirs = Second->Caps.dwCaps;

    if (((Mine ^ Theirs) & DdChainKindCaps) != 0)
        return FALSE;
    if ((Mine & Theirs & DdMipTexture) == DdMipTexture)
        return FALSE;
    return TRUE;
}

/* The first attachment that continues a flip chain */
_Ret_maybenull_
static
DdLink*
DdNextInChain(
    _In_opt_ DdSurface* Surface)
{
    DdLink* Link;

    if (Surface == nullptr)
        return nullptr;
    for (Link = Surface->Attached; Link != nullptr; Link = Link->NextOut)
    {
        if ((Link->To->Caps.dwCaps & DDSCAPS_FLIP) != 0 && DdCanFlipBetween(Surface, Link->To))
            return Link;
    }
    return nullptr;
}

_Ret_maybenull_
static
DdLink*
DdFindLink(
    _In_ DdSurface* From,
    _In_ const DdSurface* To,
    _In_ UCHAR ToVersion)
{
    DdLink* Link;

    for (Link = From->Attached; Link != nullptr; Link = Link->NextOut)
    {
        if (Link->To == To && Link->ToVersion == ToVersion)
            return Link;
    }
    return nullptr;
}

HRESULT
DdAddLink(
    _In_ DdSurface* From,
    _In_ UCHAR FromVersion,
    _In_ DdSurface* To,
    _In_ UCHAR ToVersion,
    _In_ BOOLEAN Implicit)
{
    DdLink* Link = new (std::nothrow) DdLink;

    if (Link == nullptr)
        return E_OUTOFMEMORY;

    if (!Implicit)
        DdHoldSurface(To, ToVersion);
    Link->From = From;
    Link->To = To;
    Link->FromVersion = FromVersion;
    Link->ToVersion = ToVersion;
    Link->Implicit = Implicit;
    Link->NextOut = From->Attached;
    From->Attached = Link;
    Link->NextIn = To->AttachedFrom;
    To->AttachedFrom = Link;
    return S_OK;
}

/* Unthreads one link from both lists and lets go of what it held */
static
HRESULT
DdRemoveLink(
    _In_ DdSurface* From,
    _In_ const DdSurface* To,
    _In_ UCHAR ToVersion)
{
    DdLink* Link = DdFindLink(From, To, ToVersion);
    DdLink** Slot;
    DdSurface* Target;
    UCHAR Version;
    BOOLEAN Held;

    if (Link == nullptr)
        return DDERR_SURFACENOTATTACHED;

    for (Slot = &From->Attached; *Slot != Link; Slot = &(*Slot)->NextOut)
        ;
    *Slot = Link->NextOut;
    for (Slot = &Link->To->AttachedFrom; *Slot != nullptr && *Slot != Link; Slot = &(*Slot)->NextIn)
        ;
    if (*Slot == nullptr)
    {
        delete Link;
        return DDERR_SURFACENOTATTACHED;
    }
    *Slot = Link->NextIn;

    Target = Link->To;
    Version = Link->ToVersion;
    Held = !Link->Implicit;
    delete Link;
    if (Held)
        DdDropSurface(Target, Version);
    return S_OK;
}

/* A surface leaving a chain keeps only the front or back role it was created with */
static
VOID
DdLeaveChainRole(
    _Inout_ DdSurface* Surface)
{
    Surface->Caps.dwCaps &= ~DdChainRoleCaps;
    if ((Surface->ChainFlags & DdChainWasBack) != 0)
        Surface->Caps.dwCaps |= DDSCAPS_BACKBUFFER;
    if ((Surface->ChainFlags & DdChainWasFront) != 0)
        Surface->Caps.dwCaps |= DDSCAPS_FRONTBUFFER;
}

/*
 * Removes the To attachment from From. With Mend set and To being the next surface in a flip
 * chain, the chain closes up around it; Force allows links made by complex creation to go.
 */
static
HRESULT
DdDetach(
    _In_ DdSurface* From,
    _In_ UCHAR FromVersion,
    _In_ DdSurface* To,
    _In_ UCHAR ToVersion,
    _In_ BOOLEAN Mend,
    _In_ BOOLEAN Force)
{
    DdLink* Link = DdFindLink(From, To, ToVersion);
    DdLink* Next = nullptr;
    DdLink* Walk;
    DdLink* After;
    DdSurface* Front = nullptr;
    DdSurface* Follower = nullptr;
    UCHAR FollowerVersion = 0;
    BOOLEAN HeldEnds = FALSE;
    BOOLEAN HeldFollower = FALSE;
    HRESULT hr;

    if (Link == nullptr)
        return DDERR_SURFACENOTATTACHED;
    if (Link->Implicit && !Force)
        return DDERR_CANNOTDETACHSURFACE;

    Walk = Mend ? DdNextInChain(From) : nullptr;
    if (Walk != nullptr && Walk->To == To && Walk->ToVersion == ToVersion)
        Next = DdNextInChain(To);

    if (Next != nullptr)
    {
        Follower = Next->To;
        FollowerVersion = Next->ToVersion;
        for (Walk = Next; Walk != nullptr; Walk = DdNextInChain(Walk->To))
        {
            if ((Walk->To->Caps.dwCaps & DDSCAPS_FRONTBUFFER) != 0)
                Front = Walk->To;
            if (Walk->To == To && Walk->ToVersion == ToVersion)
                break;
        }
    }

    if (Front != nullptr)
    {
        Front->BackBufferCount -= 1;
        if (Front->BackBufferCount != 0)
        {
            /* The surface after To takes its place behind From */
            DdAddLink(From, FromVersion, Follower, FollowerVersion, Link->Implicit);
            DdHoldSurface(To, ToVersion);
            DdHoldSurface(From, FromVersion);
            DdHoldSurface(Follower, FollowerVersion);
            HeldFollower = TRUE;
            DdRemoveLink(To, Follower, FollowerVersion);
            if ((To->Caps.dwCaps & DDSCAPS_BACKBUFFER) != 0)
            {
                Follower->Caps.dwCaps |= DDSCAPS_BACKBUFFER;
            }
            else if ((To->Caps.dwCaps & DDSCAPS_FRONTBUFFER) != 0)
            {
                Follower->Caps.dwCaps = (Follower->Caps.dwCaps & ~DDSCAPS_BACKBUFFER) | DDSCAPS_FRONTBUFFER;
                Follower->BackBufferCount = Front->BackBufferCount;
                After = DdNextInChain(Follower);
                if (After != nullptr)
                    After->To->Caps.dwCaps |= DDSCAPS_BACKBUFFER;
                Front->BackBufferCount = 0;
            }
        }
        else
        {
            /* Two surfaces were left, so the chain comes apart */
            DdLeaveChainRole(Follower);
            DdHoldSurface(To, ToVersion);
            DdHoldSurface(From, FromVersion);
            DdRemoveLink(To, From, FromVersion);
        }
        DdLeaveChainRole(To);
        HeldEnds = TRUE;
    }

    hr = DdRemoveLink(From, To, ToVersion);
    if (HeldEnds)
    {
        DdDropSurface(To, ToVersion);
        DdDropSurface(From, FromVersion);
    }
    if (HeldFollower)
        DdDropSurface(Follower, FollowerVersion);
    return hr;
}

/* Called as a surface goes away: what it created goes with it, and nothing keeps pointing at it */
VOID
DdDetachForDestroy(
    _Inout_ DdSurface* Surface,
    _In_ UCHAR Version)
{
    DdLink* Link;
    DdSurface* Target;
    UCHAR TargetVersion;
    BOOLEAN Implicit;
    ULONG Count;

    while ((Link = Surface->Attached) != nullptr)
    {
        Target = Link->To;
        TargetVersion = Link->ToVersion;
        Implicit = Link->Implicit;
        if (FAILED(DdDetach(Surface, Version, Target, TargetVersion, TRUE, TRUE)))
            DdRemoveLink(Surface, Target, TargetVersion);

        if (Implicit && (Target->ChainFlags & (DdChainImplicit | DdChainDying)) == DdChainImplicit)
        {
            Target->ChainFlags &= ~DdChainImplicit;
            for (Count = DdSurfaceRefsOn(Target, TargetVersion); Count > 0; Count -= 1)
                DdDropSurface(Target, TargetVersion);
        }
    }

    while ((Link = Surface->AttachedFrom) != nullptr)
    {
        if (FAILED(DdDetach(Link->From, Link->FromVersion, Surface, Link->ToVersion, TRUE, TRUE)))
            DdRemoveLink(Link->From, Surface, Link->ToVersion);
    }
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceAddAttached(
    _In_ VOID* Com,
    _In_opt_ VOID* Attach)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdSurface* Other = DdSurfaceFromCom(Attach);
    UCHAR Version = DdSurfaceVersionOf(Com);
    UCHAR OtherVersion;
    DdLink* First;
    DdLink* Walk;
    DdLink* Link;
    DdSurface* Front = nullptr;
    DdSurface* Successor;
    UCHAR SuccessorVersion;
    BOOLEAN SuccessorImplicit;
    DWORD Mine;
    DWORD Theirs;
    HRESULT hr;

    DdLockScope Lock;

    if (Surface == nullptr || Other == nullptr)
        return E_INVALIDARG;
    OtherVersion = DdSurfaceVersionOf(Attach);
    Mine = Surface->Caps.dwCaps;
    Theirs = Other->Caps.dwCaps;

    /* IDirectDrawSurface7 only takes Z buffers; the older interfaces take anything that fits */
    if (Version >= 7 && ((Mine ^ Theirs) & DDSCAPS_ZBUFFER) == 0)
        return DDERR_CANNOTATTACHSURFACE;
    if (((Mine | Theirs) & (DDSCAPS_RESERVED2 | DDSCAPS_OPTIMIZED)) != 0)
        return DDERR_CANNOTATTACHSURFACE;
    if ((Surface->Caps.dwCaps2 & DDSCAPS2_CUBEMAP) != 0 && (Theirs & DDSCAPS_ZBUFFER) == 0)
        return DDERR_CANNOTATTACHSURFACE;

    if ((Mine & DDSCAPS_PRIMARYSURFACE) != 0 && Surface->Driver->ExclusiveOwner != Surface->Creator)
        return DDERR_NOEXCLUSIVEMODE;
    if (Surface == Other)
        return DDERR_CANNOTATTACHSURFACE;
    if (Surface->Lost || Other->Lost)
        return DDERR_SURFACELOST;
    if (DdFindLink(Surface, Other, OtherVersion) != nullptr)
        return DDERR_SURFACEALREADYATTACHED;

    /* Mip levels shrink; anything else of the same kind must match in size */
    if ((Mine & Theirs & DdMipTexture) == DdMipTexture)
    {
        if (Surface->Shared->Width < Other->Shared->Width || Surface->Shared->Height < Other->Shared->Height)
            return DDERR_CANNOTATTACHSURFACE;
    }
    else if (((Mine ^ Theirs) & DDSCAPS_TEXTURE) == 0 &&
             (Surface->Shared->Width != Other->Shared->Width || Surface->Shared->Height != Other->Shared->Height))
    {
        return DDERR_CANNOTATTACHSURFACE;
    }

    if ((Theirs & DdOneOfEachCaps) != 0)
    {
        for (Link = Surface->Attached; Link != nullptr; Link = Link->NextOut)
        {
            DWORD Held = Link->To->Caps.dwCaps;

            if ((Held & Theirs & DdOneOfEachCaps) == 0)
                continue;
            if ((Held & Theirs & DDSCAPS_TEXTURE) == 0 || ((Held ^ Theirs) & DDSCAPS_MIPMAP) == 0)
                return DDERR_CANNOTATTACHSURFACE;
        }
    }

    if ((Theirs & DDSCAPS_FLIP) != 0 && DdCanFlipBetween(Surface, Other))
        return DDERR_CANNOTATTACHSURFACE;

    /* Memory kinds must agree, except that a system memory Z buffer may serve a video surface */
    if (((Mine ^ Theirs) & DDSCAPS_SYSTEMMEMORY) != 0 &&
        ((Theirs & DDSCAPS_ZBUFFER) == 0 || (Theirs & DDSCAPS_SYSTEMMEMORY) == 0))
    {
        return DDERR_CANNOTATTACHSURFACE;
    }

    if (!DdCanFlipBetween(Surface, Other))
        return DdAddLink(Surface, Version, Other, OtherVersion, FALSE);

    if ((Mine & DDSCAPS_FLIP) != 0)
    {
        /* Joining an existing chain right behind this surface */
        First = DdNextInChain(Surface);
        for (Walk = First; Walk != nullptr; Walk = DdNextInChain(Walk->To))
        {
            if ((Walk->To->Caps.dwCaps & DDSCAPS_FRONTBUFFER) != 0)
            {
                Front = Walk->To;
                break;
            }
        }
        if (First == nullptr || Front == nullptr)
            return DDERR_CANNOTATTACHSURFACE;

        Other->Caps.dwCaps &= ~(DDSCAPS_BACKBUFFER | DDSCAPS_FRONTBUFFER);
        if ((Mine & DDSCAPS_FRONTBUFFER) != 0)
        {
            Other->Caps.dwCaps |= DDSCAPS_BACKBUFFER;
            First->To->Caps.dwCaps &= ~DDSCAPS_BACKBUFFER;
        }
        Other->Caps.dwCaps |= DDSCAPS_FLIP;
        Front->BackBufferCount += 1;

        Successor = First->To;
        SuccessorVersion = First->ToVersion;
        SuccessorImplicit = First->Implicit;
        DdHoldSurface(Successor, SuccessorVersion);
        hr = DdDetach(Surface, Version, Successor, SuccessorVersion, FALSE, TRUE);
        if (SUCCEEDED(hr))
            hr = DdAddLink(Surface, Version, Other, OtherVersion, FALSE);
        if (SUCCEEDED(hr))
            hr = DdAddLink(Other, OtherVersion, Successor, SuccessorVersion, SuccessorImplicit);
        DdDropSurface(Successor, SuccessorVersion);
        return FAILED(hr) ? DDERR_CANNOTATTACHSURFACE : S_OK;
    }

    /* Two plain surfaces become a chain of their own */
    hr = DdAddLink(Surface, Version, Other, OtherVersion, FALSE);
    if (SUCCEEDED(hr))
        hr = DdAddLink(Other, OtherVersion, Surface, Version, TRUE);
    if (FAILED(hr))
        return DDERR_CANNOTATTACHSURFACE;

    if ((Mine & DDSCAPS_BACKBUFFER) != 0)
    {
        Other->Caps.dwCaps |= DDSCAPS_FRONTBUFFER;
        Other->BackBufferCount = 1;
    }
    else
    {
        Other->Caps.dwCaps |= DDSCAPS_BACKBUFFER;
        Surface->Caps.dwCaps |= DDSCAPS_FRONTBUFFER;
        Surface->BackBufferCount = 1;
    }
    Surface->Caps.dwCaps |= DDSCAPS_FLIP;
    Other->Caps.dwCaps |= DDSCAPS_FLIP;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceDeleteAttached(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ VOID* Attach)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdSurface* Other = DdSurfaceFromCom(Attach);
    UCHAR Version = DdSurfaceVersionOf(Com);
    DdLink* Link;
    DdLink* Next;
    HRESULT hr;

    DdLockScope Lock;

    if (Flags != 0)
        return E_INVALIDARG;
    if (Surface->Lost || (Other != nullptr && Other->Lost))
        return DDERR_SURFACELOST;
    if ((Surface->Caps.dwCaps & DDSCAPS_OPTIMIZED) != 0)
        return DdErrOptimizedSurface;

    if (Other != nullptr)
        return DdDetach(Surface, Version, Other, DdSurfaceVersionOf(Attach), TRUE, FALSE);

    for (Link = Surface->Attached; Link != nullptr; Link = Next)
    {
        Next = Link->NextOut;
        hr = DdDetach(Surface, Version, Link->To, Link->ToVersion, TRUE, FALSE);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* Flags Flip knows about */
static constexpr DWORD DdFlipFlags = DDFLIP_WAIT | DDFLIP_EVEN | DDFLIP_ODD | DDFLIP_NOVSYNC | DDFLIP_STEREO |
                                     DDFLIP_DONOTWAIT | DDFLIP_INTERVAL2 | DDFLIP_INTERVAL3 | DDFLIP_INTERVAL4;

static constexpr DWORD DdFlipIntervals = DDFLIP_INTERVAL2 | DDFLIP_INTERVAL3 | DDFLIP_INTERVAL4;

template <typename T>
static
inline
VOID
DdTrade(
    _Inout_ T& First,
    _Inout_ T& Second)
{
    T Held = First;

    First = Second;
    Second = Held;
}

/* Trades the memory behind two surfaces; everything else stays with the surface */
static
VOID
DdTradeMemory(
    _Inout_ DdSurface* First,
    _Inout_ DdSurface* Second)
{
    DdTrade(First->Shared->Bits, Second->Shared->Bits);
    DdTrade(First->Shared->Section, Second->Shared->Section);
    DdTrade(First->Resource, Second->Resource);
    DdTrade(First->Shared->SystemResource, Second->Shared->SystemResource);
    DdTrade(First->Shared->DriverPending, Second->Shared->DriverPending);
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceFlip(
    _In_ VOID* Com,
    _In_opt_ VOID* Target,
    _In_ DWORD Flags)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdSurface* Wanted = DdSurfaceFromCom(Target);
    DdSurface* Partner = nullptr;
    DdSurface* Member;
    DdLink* Link;
    DWORD Caps;

    DdLockScope Lock;

    if ((Flags & ~DdFlipFlags) != 0)
        return E_INVALIDARG;
    Caps = Surface->Caps.dwCaps;
    if ((Flags & (DDFLIP_EVEN | DDFLIP_ODD)) != 0 && (Caps & DDSCAPS_OVERLAY) == 0)
        return E_INVALIDARG;
    if ((Flags & DDFLIP_NOVSYNC) != 0 && (Flags & DdFlipIntervals) != 0)
        return E_INVALIDARG;
    if (Surface->Lost || (Wanted != nullptr && Wanted->Lost))
        return DDERR_SURFACELOST;
    if ((Flags & DDFLIP_STEREO) != 0)
        return E_INVALIDARG;
    if ((Caps & DDSCAPS_FRONTBUFFER) == 0 || (Caps & DDSCAPS_FLIP) == 0)
        return DDERR_NOTFLIPPABLE;
    if (DdSurfaceBusy(Surface))
        return DDERR_SURFACEBUSY;
    if ((Caps & DDSCAPS_PRIMARYSURFACE) != 0 && Surface->Driver->ExclusiveOwner != Surface->Creator)
        return DDERR_NOEXCLUSIVEMODE;

    /* Every surface behind the front must be free, at least the ones this flip touches */
    Link = DdNextInChain(Surface);
    if (Link == nullptr)
        return DDERR_NOTFLIPPABLE;
    for (; Link != nullptr && Link->To != Surface; Link = DdNextInChain(Link->To))
    {
        Member = Link->To;
        if (Member->Lost)
            return DDERR_SURFACELOST;
        if (Partner == nullptr)
            Partner = Member;
        if (DdSurfaceBusy(Member) && (Wanted == nullptr || Wanted == Member))
            return DDERR_SURFACEBUSY;
        if (Wanted == Member)
            Partner = Member;
    }
    if (Wanted != nullptr && Partner != Wanted)
        return DDERR_NOTFLIPPABLE;
    if (((Partner->Caps.dwCaps ^ Caps) & DDSCAPS_SYSTEMMEMORY) != 0)
        return E_INVALIDARG;

    if ((Caps & DDSCAPS_PRIMARYSURFACE) != 0)
    {
        DD_TRACE_ONCE(L"flipping the primary is not implemented yet");
        return DDERR_UNSUPPORTED;
    }

    DdTouchSurface(Surface);
    DdTouchSurface(Partner);

    /* A named target trades with the front; otherwise the memory moves one step along the chain */
    if (Wanted != nullptr)
    {
        DdTradeMemory(Surface, Wanted);
        return S_OK;
    }
    for (Link = DdNextInChain(Surface); Link != nullptr && Link->To != Surface; Link = DdNextInChain(Link->To))
        DdTradeMemory(Link->From, Link->To);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceGetFlipStatus(
    _In_ VOID* Com,
    _In_ DWORD Flags)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if (Flags != DDGFS_CANFLIP && Flags != DDGFS_ISFLIPDONE)
        return E_INVALIDARG;
    if ((Surface->Caps.dwCaps & DDSCAPS_RESERVED2) != 0)
        return DDERR_INVALIDSURFACETYPE;
    if (Surface->Lost)
        return DDERR_SURFACELOST;
    return S_OK;
}

/* The interface version a caller of this version gets back for an attached surface */
static
UCHAR
DdVersionHandedOut(
    _In_ UCHAR Version)
{
    return Version <= 3 ? 1 : Version;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceEnumAttached(
    _In_ VOID* Com,
    _In_opt_ VOID* Context,
    _In_opt_ VOID* Callback)
{
    LPDDENUMSURFACESCALLBACK7 Report = reinterpret_cast<LPDDENUMSURFACESCALLBACK7>(Callback);
    DdSurface* Surface = DdSurfaceFromCom(Com);
    UCHAR Version = DdVersionHandedOut(DdSurfaceVersionOf(Com));
    DDSURFACEDESC2 Desc;
    DdLink* Link;
    VOID* Found;

    DdLockScope Lock;

    if (Report == nullptr)
        return E_INVALIDARG;
    if (Surface->Lost)
        return DDERR_SURFACELOST;

    for (Link = Surface->Attached; Link != nullptr; Link = Link->NextOut)
    {
        /* Older callers get the short description over the same layout */
        DdDescribeSurface(Link->To, &Desc);
        if (Version == 1)
            Desc.dwSize = sizeof(DDSURFACEDESC);
        Found = DdHandOutSurface(Link->To, Version);
        if (Report(static_cast<LPDIRECTDRAWSURFACE7>(Found), &Desc, Context) == DDENUMRET_CANCEL)
            break;
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceGetAttached(
    _In_ VOID* Com,
    _In_opt_ VOID* Caps,
    _Out_opt_ VOID** Result)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    UCHAR Version = DdVersionHandedOut(DdSurfaceVersionOf(Com));
    DDSCAPS2 Want = {};
    DDSCAPS2 Has;
    DdLink* Link;

    DdLockScope Lock;

    if (Caps == nullptr || Result == nullptr)
        return E_INVALIDARG;
    if (Surface->Lost)
        return DDERR_SURFACELOST;
    if (Version >= 7)
        Want = *static_cast<const DDSCAPS2*>(Caps);
    else
        Want.dwCaps = static_cast<const DDSCAPS*>(Caps)->dwCaps;
    if (Version >= 4 && (Want.dwCaps & DdReservedCaps) != 0)
        return DDERR_INVALIDCAPS;
    *Result = nullptr;

    /* The first attachment that has every asked for cap, when anything was asked for */
    for (Link = Surface->Attached; Link != nullptr; Link = Link->NextOut)
    {
        Has.dwCaps = Link->To->Caps.dwCaps & Want.dwCaps;
        Has.dwCaps2 = Link->To->Caps.dwCaps2 & Want.dwCaps2;
        Has.dwCaps3 = Link->To->Caps.dwCaps3 & Want.dwCaps3;
        Has.dwCaps4 = Link->To->Caps.dwCaps4 & Want.dwCaps4;
        if ((Has.dwCaps | Has.dwCaps2 | Has.dwCaps3 | Has.dwCaps4) == 0)
            continue;
        if (Has.dwCaps == Want.dwCaps && Has.dwCaps2 == Want.dwCaps2 && Has.dwCaps3 == Want.dwCaps3 &&
            Has.dwCaps4 == Want.dwCaps4)
        {
            *Result = DdHandOutSurface(Link->To, Version);
            return *Result != nullptr ? S_OK : E_OUTOFMEMORY;
        }
    }
    return DDERR_NOTFOUND;
}
