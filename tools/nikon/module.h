/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NIKON_MODULE_TOOL_H
#define NIKON_MODULE_TOOL_H

#include <glib.h>

#define NIKON_MODULE_SLOT_SIZE 0x180000u
#define NIKON_MODULE_PROFILE "cold-neutral-resource80"
#define NIKON_MODULE_REGIONS_PROFILE "neutral-regions-descriptor17"
#define NIKON_MODULE_GROUP_PROFILE "functional-group-descriptor17"
#define NIKON_DESCRIPTOR17_SLOT_SIZE 0x20000u
#define NIKON_DESCRIPTOR18_SLOT_SIZE 0xa0000u
#define NIKON_MODULE_ACCEL_PROFILE "acceleration-descriptor18"

/* Explicit synthetic NOR input; not factory data or a guest RAM image. */
GBytes *nikon_module_cold_neutral(void);

/* Preserve the opaque descriptor except resource35's declared virtual body. */
GBytes *nikon_module_neutral_regions(GBytes *descriptor, GError **error);
GBytes *nikon_module_functional_group(GBytes *descriptor, GError **error);

/*
 * Import only F025's 192-byte ACCELE record. raw12 explicitly declares the
 * coordinate mapping full16 = 16 * raw12; it does not infer a physical range.
 */
GBytes *nikon_module_acceleration(GBytes *descriptor, GBytes *capture,
                                 gboolean raw12, GError **error);

#endif
