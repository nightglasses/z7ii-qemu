/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NIKON_MODULE_TOOL_H
#define NIKON_MODULE_TOOL_H

#include <glib.h>

#define NIKON_MODULE_SLOT_SIZE 0x180000u
#define NIKON_MODULE_PROFILE "cold-neutral-resource80"

/* Explicit synthetic NOR input; not factory data or a guest RAM image. */
GBytes *nikon_module_cold_neutral(void);

#endif
