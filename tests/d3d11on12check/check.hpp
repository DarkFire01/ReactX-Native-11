/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     State the Direct3D 11 on 12 checks share
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d12.h>
#include <d3d11_4.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct OnCheck
{
    PFN_D3D11ON12_CREATE_DEVICE CreateDevice = nullptr;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    PCSTR CheckName = "";

    VOID
    Report(
        _In_z_ PCSTR Key,
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...)
    {
        va_list Arguments;

        printf("%s.%s = ", CheckName, Key);
        va_start(Arguments, Format);
        vprintf(Format, Arguments);
        va_end(Arguments);
        printf("\n");
    }

    HRESULT
    Make(
        _In_ UINT Flags,
        _Outptr_ ID3D11Device** Out,
        _Out_opt_ D3D_FEATURE_LEVEL* Level = nullptr)
    {
        IUnknown* Queues[] = { Queue.Get() };

        return CreateDevice(Device.Get(), Flags, nullptr, 0, Queues, 1, 0, Out, nullptr, Level);
    }

    /* The reference count an object has right now */
    static
    ULONG
    Count(
        _In_ IUnknown* Object)
    {
        Object->AddRef();
        return Object->Release();
    }
};

/* Prints a recorded trace as numbered lines under Key */
VOID
OcPrintTrace(
    _In_ OnCheck& Check,
    _In_z_ PCSTR Key,
    _In_ const std::vector<std::string>& Lines);

VOID
OcCheckResources(
    _In_ OnCheck& Check);

VOID
OcCheckViews(
    _In_ OnCheck& Check);

VOID
OcCheckShaders(
    _In_ OnCheck& Check);

VOID
OcCheckDraw(
    _In_ OnCheck& Check);

VOID
OcCheckCopies(
    _In_ OnCheck& Check);

VOID
OcCheckMap(
    _In_ OnCheck& Check);

VOID
OcCheckQueries(
    _In_ OnCheck& Check);

VOID
OcCheckDeferred(
    _In_ OnCheck& Check);

VOID
OcCheckPresent(
    _In_ OnCheck& Check);

VOID
OcCheckGenMips(
    _In_ OnCheck& Check);

VOID
OcCheckResolve(
    _In_ OnCheck& Check);

VOID
OcCheckTiled(
    _In_ OnCheck& Check);

VOID
OcCheckVideo(
    _In_ OnCheck& Check);
