/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Entry points into the vendored DirectXShaderCompiler converter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <objidl.h>

/* Sets up the allocator slot and the per thread file system the converter uses */
BOOL
DxcUpstreamAttach(VOID);

VOID
DxcUpstreamDetach(VOID);

/** Creates hlsl::DxbcConverter with Malloc as its allocator, or the default one when null */
HRESULT
DxcUpstreamCreateConverter(
    _In_opt_ IMalloc* Malloc,
    _In_ REFIID Riid,
    _COM_Outptr_ void** Object);
