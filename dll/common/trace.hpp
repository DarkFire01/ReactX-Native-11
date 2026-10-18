/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Diagnostic output, enabled by setting REACTX_LOG
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Each module that links the trace code names itself for the line prefix */
extern const WCHAR RxTraceModule[];

BOOLEAN
RxIsTraceEnabled(VOID);

VOID
RxTrace(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...);

#define RX_TRACE(...) \
    do \
    { \
        if (RxIsTraceEnabled()) \
            RxTrace(__VA_ARGS__); \
    } while (0)

/* Names an unimplemented method the first time it is reached */
#define RX_UNIMPLEMENTED() \
    do \
    { \
        static LONG Reported; \
        if (RxIsTraceEnabled() && InterlockedExchange(&Reported, 1) == 0) \
            RxTrace(L"unimplemented %S", __FUNCTION__); \
    } while (0)
