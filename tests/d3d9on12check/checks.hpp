/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Check groups of d3d9on12check kept in their own files
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "hooks.hpp"

/* Draws of every kind into the back buffer, each read back */
VOID
CkDraws(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Back);
