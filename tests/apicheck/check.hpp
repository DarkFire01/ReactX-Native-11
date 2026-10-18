/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     API behavior checks that print comparable facts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

/*
 * Checks print facts as "check.key = value" lines, never pointers, so two runtimes
 * can be diffed. Expect adds a FAIL line when something every runtime must do is wrong.
 */
class CheckContext
{
public:
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* Context = nullptr;
    PCSTR CheckName = "";
    UINT Failures = 0;

    VOID
    Report(
        _In_z_ PCSTR Key,
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...);

    BOOLEAN
    Expect(
        _In_ BOOLEAN Condition,
        _In_z_ PCSTR What);
};

struct CheckInfo
{
    PCSTR Name;
    VOID (*Run)(CheckContext& Check);
};

/* Each file of checks hands out its table */
const CheckInfo*
AcStateChecks(
    _Out_ UINT* Count);

const CheckInfo*
AcComputeChecks(
    _Out_ UINT* Count);

const CheckInfo*
AcFormatChecks(
    _Out_ UINT* Count);

const CheckInfo*
AcInterfaceChecks(
    _Out_ UINT* Count);

/* Reference count of a COM object, read without changing it */
ULONG
AcRefCount(
    _In_ IUnknown* Object);
