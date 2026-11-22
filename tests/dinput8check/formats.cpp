/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The predefined data formats applications link from dinput8.lib
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8check.hpp"

/* Objects a format may ask for that the device need not have */
#define DC_OPTIONAL         0x80000000
#define DC_AXIS             (DIDFT_AXIS | DIDFT_ANYINSTANCE)
#define DC_BUTTON           (DIDFT_BUTTON | DIDFT_ANYINSTANCE)

static DIOBJECTDATAFORMAT DcKeyboardObjects[256];

static DIOBJECTDATAFORMAT DcMouseObjects[] =
{
    { &GUID_XAxis, 0, DC_AXIS, 0 },
    { &GUID_YAxis, 4, DC_AXIS, 0 },
    { &GUID_ZAxis, 8, DC_OPTIONAL | DC_AXIS, 0 },
    { nullptr, 12, DC_BUTTON, 0 },
    { nullptr, 13, DC_BUTTON, 0 },
    { nullptr, 14, DC_OPTIONAL | DC_BUTTON, 0 },
    { nullptr, 15, DC_OPTIONAL | DC_BUTTON, 0 },
};

static DIOBJECTDATAFORMAT DcMouse2Objects[] =
{
    { &GUID_XAxis, 0, DC_AXIS, 0 },
    { &GUID_YAxis, 4, DC_AXIS, 0 },
    { &GUID_ZAxis, 8, DC_OPTIONAL | DC_AXIS, 0 },
    { nullptr, 12, DC_BUTTON, 0 },
    { nullptr, 13, DC_BUTTON, 0 },
    { nullptr, 14, DC_OPTIONAL | DC_BUTTON, 0 },
    { nullptr, 15, DC_OPTIONAL | DC_BUTTON, 0 },
    { nullptr, 16, DC_OPTIONAL | DC_BUTTON, 0 },
    { nullptr, 17, DC_OPTIONAL | DC_BUTTON, 0 },
    { nullptr, 18, DC_OPTIONAL | DC_BUTTON, 0 },
    { nullptr, 19, DC_OPTIONAL | DC_BUTTON, 0 },
};

DIDATAFORMAT DcKeyboardFormat = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 256, 256, DcKeyboardObjects };
DIDATAFORMAT DcMouseFormat = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, sizeof(DIMOUSESTATE),
                               ARRAYSIZE(DcMouseObjects), DcMouseObjects };
DIDATAFORMAT DcMouse2Format = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, sizeof(DIMOUSESTATE2),
                                ARRAYSIZE(DcMouse2Objects), DcMouse2Objects };

VOID
DcInitFormats(VOID)
{
    for (DWORD Index = 0; Index < 256; Index++)
    {
        DcKeyboardObjects[Index].pguid = &GUID_Key;
        DcKeyboardObjects[Index].dwOfs = Index;
        DcKeyboardObjects[Index].dwType = DC_OPTIONAL | DIDFT_BUTTON | DIDFT_MAKEINSTANCE(Index);
        DcKeyboardObjects[Index].dwFlags = 0;
    }
}
