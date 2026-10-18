/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The display configuration calls, found at run time so systems without them still load us
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

struct RxDisplayConfigRoutines
{
    decltype(&GetDisplayConfigBufferSizes) GetBufferSizes;
    decltype(&QueryDisplayConfig) Query;
    decltype(&DisplayConfigGetDeviceInfo) GetDeviceInfo;
};

/* NULL when user32 does not have all three */
_Ret_maybenull_
const RxDisplayConfigRoutines*
RxGetDisplayConfig(VOID);
