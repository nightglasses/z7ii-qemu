/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_SLA_H
#define HW_NIKON_SLA_H

#include "qemu/typedefs.h"
#include <glib.h>

#define TYPE_NIKON_SLA_DOMAIN "nikon-sla-domain"

/* The domain refs payload; interfaces are borrowed for its owned lifetime. */
DeviceState *nikon_sla_create(Object *parent, const char *name,
                             GBytes *payload, unsigned num_cpus,
                             Chardev *serial, Error **errp);
AddressSpace *nikon_sla_address_space(DeviceState *domain);
DeviceState *nikon_sla_bridge(DeviceState *domain);
/* Named GPIO output peer-ready[0] forwards original GPIO8[10] guest output. */

/* Loaded C1.70 SLA software-domain profile; these are not board reset facts. */
#define NIKON_SLA_ROM_BASE 0x10020000ULL
#define NIKON_SLA_ROM_SIZE 0x86cd40ULL
#define NIKON_SLA_ENTRY 0x1002bcd4ULL
#define NIKON_SLA_COPY1_BASE 0xc1000000ULL
#define NIKON_SLA_COPY1_SIZE 0x17d60ULL
#define NIKON_SLA_COPY2_BASE 0xc1920000ULL
#define NIKON_SLA_COPY2_SIZE 0xad4ULL

#endif
