/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Entry points into the vendored DirectXShaderCompiler converter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"

#include "dxc/Support/Global.h"
#include "dxc/Support/WinIncludes.h"

#include "upstream.hpp"

/* Lives in projects/dxilconv/lib/DxbcConverter/DxbcConverter.cpp */
HRESULT CreateDxbcConverter(REFIID riid, LPVOID* ppv);

BOOL
DxcUpstreamAttach(VOID)
{
    if (FAILED(DxcInitThreadMalloc()))
        return FALSE;

    DxcSetThreadMallocToDefault();
    if (llvm::sys::fs::SetupPerThreadFileSystem())
    {
        DxcClearThreadMalloc();
        DxcCleanupThreadMalloc();
        return FALSE;
    }

    DxcClearThreadMalloc();
    return TRUE;
}

VOID
DxcUpstreamDetach(VOID)
{
    DxcSetThreadMallocToDefault();
    llvm::sys::fs::CleanupPerThreadFileSystem();
    llvm::llvm_shutdown();
    DxcClearThreadMalloc();
    DxcCleanupThreadMalloc();
}

HRESULT
DxcUpstreamCreateConverter(
    _In_opt_ IMalloc* Malloc,
    _In_ REFIID Riid,
    _COM_Outptr_ void** Object)
{
    DxcThreadMalloc Scope(Malloc);

    *Object = nullptr;
    return CreateDxbcConverter(Riid, Object);
}
