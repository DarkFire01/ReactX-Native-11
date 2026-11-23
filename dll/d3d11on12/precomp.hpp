/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Common includes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>

#include <dxgi1_6.h>
#include <d3dkmthk.h>
#include <d3d12.h>
#include <d3d12compatibility.h>
#include <d3d11_4.h>
#include <d3d10umddi.h>

#include <new>

#include "rxcomptr.hpp"
#include "d3d11on12ddi.hpp"
#include "trace.hpp"
