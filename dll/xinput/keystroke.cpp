/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Keystrokes made from controller state: presses, releases and repeats
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xinput.hpp"

/* Past this the stick counts as pushed, and a trigger past its threshold as pressed */
#define XI_THUMB_THRESHOLD      20000
#define XI_TRIGGER_THRESHOLD    30

/* Repeats come after 400 ms, then every 100 ms; one repeat timer serves every controller */
static WORD XiRepeatKey;
static DWORD XiRepeatTick;
static BOOL XiFirstRepeat;

/* The eight directions of a stick, as offsets from its UP key */
static
WORD
XiThumbKey(
    _In_ WORD Up,
    _In_ SHORT X,
    _In_ SHORT Y)
{
    WORD Offset;

    if (Y > XI_THUMB_THRESHOLD)
        Offset = (X < -XI_THUMB_THRESHOLD) ? 4 : ((X > XI_THUMB_THRESHOLD) ? 5 : 0);
    else if (Y < -XI_THUMB_THRESHOLD)
        Offset = (X > XI_THUMB_THRESHOLD) ? 6 : ((X < -XI_THUMB_THRESHOLD) ? 7 : 1);
    else if (X > XI_THUMB_THRESHOLD)
        Offset = 2;
    else if (X < -XI_THUMB_THRESHOLD)
        Offset = 3;
    else
        return 0;
    return Up + Offset;
}

/* The key for each button bit; the bits with no key never read as pressed */
static
WORD
XiButtonKey(
    _In_ UINT Bit,
    _In_ WORD Previous)
{
    switch (Bit)
    {
        case 0:
            return VK_PAD_DPAD_UP;
        case 8:
            return VK_PAD_LSHOULDER;
        case 9:
            return VK_PAD_RSHOULDER;
        case 12:
            return VK_PAD_A;
        default:
            return Previous + 1;
    }
}

static
DWORD
XiReleased(
    _Inout_ PXINPUT_KEYSTROKE Keystroke,
    _In_ WORD Key)
{
    Keystroke->VirtualKey = Key;
    Keystroke->Flags = XINPUT_KEYSTROKE_KEYUP;
    XiRepeatKey = 0;
    return ERROR_SUCCESS;
}

/*
 * One keystroke per call: releases first, then a new press, then a repeat of the last press.
 * A held right trigger hides the left one.
 */
DWORD
XiNextKeystroke(
    _Inout_ PXI_DEVICE Device,
    _Inout_ PXINPUT_KEYSTROKE Keystroke)
{
    XINPUT_STATE State = {};
    WORD Buttons;
    WORD Pressed = 0;
    WORD Key;
    UINT Triggers;
    DWORD Tick;

    if (FAILED(XiGetStateRoutine(Device, &State)))
        return ERROR_EMPTY;

    Buttons = State.Gamepad.wButtons;
    Keystroke->UserIndex = 0;
    Keystroke->Unicode = 0;

    Key = XiThumbKey(VK_PAD_LTHUMB_UP, State.Gamepad.sThumbLX, State.Gamepad.sThumbLY);
    Keystroke->VirtualKey = Key;
    if (Device->LeftThumbKey != Key)
    {
        if (Device->LeftThumbKey != 0)
        {
            Key = Device->LeftThumbKey;
            Device->LeftThumbKey = 0;
            return XiReleased(Keystroke, Key);
        }
        Pressed = Key;
        Device->LeftThumbKey = Key;
    }

    Key = XiThumbKey(VK_PAD_RTHUMB_UP, State.Gamepad.sThumbRX, State.Gamepad.sThumbRY);
    if (Key != 0)
        Keystroke->VirtualKey = Key;
    if (Device->RightThumbKey != Key)
    {
        if (Device->RightThumbKey != 0)
        {
            Key = Device->RightThumbKey;
            Device->RightThumbKey = 0;
            return XiReleased(Keystroke, Key);
        }
        Pressed = Key;
        Device->RightThumbKey = Key;
    }

    if (State.Gamepad.bRightTrigger > XI_TRIGGER_THRESHOLD)
        Triggers = 2;
    else
        Triggers = (State.Gamepad.bLeftTrigger > XI_TRIGGER_THRESHOLD) ? 1 : 0;
    for (UINT Bit = 0; Bit < 2; Bit++)
    {
        UCHAR Mask = (UCHAR)(1 << Bit);

        Key = (WORD)(VK_PAD_LTRIGGER + Bit);
        if (Device->TriggerKeys & Mask)
        {
            if (!(Triggers & Mask))
            {
                Device->TriggerKeys &= (UCHAR)~Mask;
                return XiReleased(Keystroke, Key);
            }
            Keystroke->VirtualKey = Key;
        }
        else if (Triggers & Mask)
        {
            Device->TriggerKeys |= Mask;
            Pressed = Key;
        }
    }

    Key = VK_PAD_DPAD_UP;
    for (UINT Bit = 0; Bit < 16; Bit++)
    {
        WORD Mask = (WORD)(1 << Bit);

        Key = XiButtonKey(Bit, Key);
        if (Device->ButtonKeys & Mask)
        {
            if (!(Buttons & Mask & ~XI_GAMEPAD_GUIDE))
            {
                Device->ButtonKeys &= (WORD)~Mask;
                return XiReleased(Keystroke, Key);
            }
            Keystroke->VirtualKey = Key;
        }
        else if (Buttons & Mask & ~XI_GAMEPAD_GUIDE)
        {
            Device->ButtonKeys |= Mask;
            Pressed = Key;
        }
    }

    Tick = GetTickCount();
    if (Pressed != 0)
    {
        Keystroke->Flags = XINPUT_KEYSTROKE_KEYDOWN;
        XiRepeatKey = Pressed;
        XiRepeatTick = Tick;
        XiFirstRepeat = TRUE;
        Keystroke->VirtualKey = Pressed;
        return ERROR_SUCCESS;
    }

    Key = Keystroke->VirtualKey;
    if (Key != XiRepeatKey)
    {
        Keystroke->VirtualKey = 0;
        return ERROR_EMPTY;
    }
    if (Tick - XiRepeatTick < (XiFirstRepeat ? 400u : 100u))
    {
        Keystroke->VirtualKey = 0;
        Key = 0;
    }
    else
    {
        XiRepeatTick = Tick;
        XiFirstRepeat = FALSE;
        Keystroke->Flags = XINPUT_KEYSTROKE_KEYDOWN | XINPUT_KEYSTROKE_REPEAT;
    }
    return (Key != 0) ? ERROR_SUCCESS : ERROR_EMPTY;
}
