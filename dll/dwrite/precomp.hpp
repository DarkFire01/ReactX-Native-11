/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Common includes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

/* This module defines DWriteCreateFactory, so the header must not import it */
#define DWRITE_EXPORT WINAPI
#include <dwrite_3.h>

#include <new>
#include <vector>
#include <algorithm>

#include "trace.hpp"

#include "rxcomptr.hpp"
