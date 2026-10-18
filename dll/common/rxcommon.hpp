/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Includes every ReactX runtime module starts from
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

#include <new>

#include "trace.hpp"
#include "kmt.hpp"
#include "displayconfig.hpp"
