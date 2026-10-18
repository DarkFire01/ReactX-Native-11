/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runtime callbacks shared between the WDDM 1.x and 2.x files
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Maps runtime resource handles to kernel resources; the caller frees with delete[] */
#include "kmcallbacks.hpp"

