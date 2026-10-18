/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The display configuration calls, found at run time so systems without them still load us
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"
#include "displayconfig.hpp"

_Ret_maybenull_
const RxDisplayConfigRoutines*
RxGetDisplayConfig(VOID)
{
    static const RxDisplayConfigRoutines Routines = []()
    {
        RxDisplayConfigRoutines Found = {};
        HMODULE User32 = GetModuleHandleW(L"user32.dll");

        if (User32 != NULL)
        {
            Found.GetBufferSizes = reinterpret_cast<decltype(Found.GetBufferSizes)>(
                GetProcAddress(User32, "GetDisplayConfigBufferSizes"));
            Found.Query = reinterpret_cast<decltype(Found.Query)>(GetProcAddress(User32, "QueryDisplayConfig"));
            Found.GetDeviceInfo = reinterpret_cast<decltype(Found.GetDeviceInfo)>(
                GetProcAddress(User32, "DisplayConfigGetDeviceInfo"));
        }

        return Found;
    }();

    if (Routines.GetBufferSizes == nullptr || Routines.Query == nullptr || Routines.GetDeviceInfo == nullptr)
        return nullptr;

    return &Routines;
}
