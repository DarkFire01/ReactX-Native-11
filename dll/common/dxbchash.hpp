/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The checksum a DXBC container carries after its magic
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Hashes everything after the checksum field: the container from byte 20 to its end */
VOID
RxDxbcHash(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ UINT Size,
    _Out_writes_bytes_all_(16) BYTE* Digest);
