/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     In process tracer of the D3D12 calls a layer makes on a device and its queue
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <string>
#include <vector>

/* Points the vtables of the device, the queue and every object they create at logging wrappers */
BOOLEAN
Tr12Install(
    _In_ ID3D12Device* Device,
    _In_ ID3D12CommandQueue* Queue);

/* Forgets every name but the test's own, so a scenario numbers its objects from zero */
VOID
Tr12Reset(VOID);

/* Starts recording calls; objects seen so far keep their names */
VOID
Tr12Begin(VOID);

/* Stops recording and hands back the lines */
std::vector<std::string>
Tr12End(VOID);

/* Names an object the test made itself so the trace can refer to it */
VOID
Tr12Name(
    _In_ IUnknown* Object,
    _In_z_ PCSTR Name);
