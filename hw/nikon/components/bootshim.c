/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/nikon/bootshim.h"

typedef struct NikonBootShimRange {
    const char *name;
    hwaddr base;
    uint64_t size;
} NikonBootShimRange;

static const NikonBootShimRange mas_ranges[] = {
    [NIKON_BOOT_SHIM_MAS_COPY] = {
        "nikon.bootshim.mas-copy", 0x50200000, 0x27800,
    },
    [NIKON_BOOT_SHIM_MAS_STAGE] = {
        "nikon.bootshim.mas-stage", 0x50000000, 0x57e30,
    },
    [NIKON_BOOT_SHIM_MAS_STAGE_PLANE] = {
        "nikon.bootshim.mas-stage-plane", 0x50080000, 0x69e70,
    },
    [NIKON_BOOT_SHIM_MAS_CODE] = {
        "nikon.bootshim.mas-code", 0xc8200000, 0x300000,
    },
    [NIKON_BOOT_SHIM_MAS_INNER] = {
        /* MAS inner image: O[0x208853c,0x228cf3c), loaded by guest COPY. */
        "nikon.bootshim.mas-inner", 0xb1000000, 0x204a00,
    },
    [NIKON_BOOT_SHIM_MAS_INNER_PREFIX] = {
        /* Same source supplies the separate 64-byte early handoff. */
        "nikon.bootshim.mas-inner-prefix", 0x04008000, 0x40,
    },
};

bool nikon_bootshim_is_functional(const char *profile)
{
    return profile && !strcmp(profile, NIKON_BOOT_SHIM_FUNCTIONAL_V1);
}

void nikon_bootshim_prepare_mas(const char *profile, MemoryRegion *root,
                                MemoryRegion views[NIKON_BOOT_SHIM_MAS_VIEW_COUNT],
                                Error **errp)
{
    unsigned i;

    if (!profile || !strcmp(profile, "none")) {
        return;
    }
    if (!nikon_bootshim_is_functional(profile)) {
        error_setg(errp, "unsupported Nikon boot-shim profile '%s'", profile);
        return;
    }
    for (i = 0; i < NIKON_BOOT_SHIM_MAS_VIEW_COUNT; i++) {
        const NikonBootShimRange *range = &mas_ranges[i];

        memory_region_init_ram(&views[i], NULL, range->name, range->size,
                               &error_fatal);
        memory_region_add_subregion(root, range->base, &views[i]);
    }
}

void nikon_bootshim_prepare_sla(const char *profile, MemoryRegion *root,
                                MemoryRegion *copy, Error **errp)
{
    if (!profile || !strcmp(profile, "none")) {
        return;
    }
    if (!nikon_bootshim_is_functional(profile)) {
        error_setg(errp, "unsupported Nikon boot-shim profile '%s'", profile);
        return;
    }
    memory_region_init_ram(copy, NULL, "nikon.bootshim.sla-copy", 0x27800,
                           &error_fatal);
    memory_region_add_subregion(root, 0x80300000, copy);
}
