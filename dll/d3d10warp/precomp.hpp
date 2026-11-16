/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Common includes for the software rasterizer driver
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <d3d10umddi.h>
#include <new>
#include <string.h>
#include <math.h>

/* Every DDI entry point runs under one process wide lock */
class RxWarpLock
{
public:
    RxWarpLock();
    ~RxWarpLock();
};

/* The newest device, for D3D11RefGetLastCreation */
extern VOID *RxWarpLastDevice;

/* Driver objects live on our heap; the runtime's per object memory holds a pointer to them */
template <typename Object, typename Handle>
inline
Object *
RxWarpObject(
    _In_ Handle Value)
{
    return Value.pDrvPrivate ? *static_cast<Object **>(Value.pDrvPrivate) : nullptr;
}

template <typename Object, typename Handle>
inline
VOID
RxWarpBind(
    _In_ Handle Value,
    _In_ Object *Instance)
{
    *static_cast<Object **>(Value.pDrvPrivate) = Instance;
}

/* Size of the runtime's per object memory: one pointer */
static constexpr SIZE_T RxWarpHandleSize = sizeof(VOID *);

#ifdef REACTX_WARP_TRACE
VOID RxWarpTrace(_In_z_ PCSTR Format, ...);
#define RX_WARP_TRACE(...) RxWarpTrace(__VA_ARGS__)
#else
#define RX_WARP_TRACE(...) ((void)0)
#endif
