/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_BOOTSHIM_H
#define HW_NIKON_BOOTSHIM_H

#include "system/memory.h"
#include "qapi/error.h"

#define NIKON_BOOT_SHIM_FUNCTIONAL_V1 "functional-v1"

enum NikonBootShimMasView {
    NIKON_BOOT_SHIM_MAS_COPY,
    NIKON_BOOT_SHIM_MAS_STAGE,
    NIKON_BOOT_SHIM_MAS_STAGE_PLANE,
    NIKON_BOOT_SHIM_MAS_CODE,
    NIKON_BOOT_SHIM_MAS_VIEW_COUNT,
};

bool nikon_bootshim_is_functional(const char *profile);
void nikon_bootshim_prepare_mas(const char *profile, MemoryRegion *root,
                                MemoryRegion views[NIKON_BOOT_SHIM_MAS_VIEW_COUNT],
                                Error **errp);
void nikon_bootshim_prepare_sla(const char *profile, MemoryRegion *root,
                                MemoryRegion *copy, Error **errp);

#endif
