/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Keyboard layouts and the scan code tables behind them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "dinput8.hpp"

#define DI_KEY_TABLE_COUNT          9

/* The translation used when the layout needs none */
#define DI_KEY_TABLE_IDENTITY       6

/* Key family bits in DiKeyFamilies */
#define DI_KEYS_XT                  0x01
#define DI_KEYS_AT                  0x02
#define DI_KEYS_ENHANCED            0x04
#define DI_KEYS_JAPANESE            0x2F
#define DI_KEYS_TOGGLE              0x30

extern const BYTE DiKeyFamilies[256];
extern const BYTE DiKeyTranslations[DI_KEY_TABLE_COUNT][256];

/* Hardware scan code of each keyboard object, by object index */
extern BYTE DiKeyHardwareCodes[256];

/* Hardware to DirectInput scan code translation of the system keyboard */
extern const BYTE* DiKeyTranslation;

DWORD
DiGetKeyboardType(
    _In_ INT Which);
