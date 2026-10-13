/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_SLA_H
#define HW_NIKON_SLA_H

#include <stdbool.h>
#include "qemu/typedefs.h"
#include <glib.h>

#define TYPE_NIKON_SLA_DOMAIN "nikon-sla-domain"

/* The domain refs payload and optional prefix; interfaces remain borrowed. */
DeviceState *nikon_sla_create(Object *parent, const char *name,
                             GBytes *payload, GBytes *boot_prefix,
                             const char *boot_shim,
                             bool bridge_notifications,
                             unsigned num_cpus,
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
#define NIKON_SLA_DEFERRED_SOURCE 0x10874d40ULL
#define NIKON_SLA_DEFERRED_BASE 0xc1920b00ULL
#define NIKON_SLA_DEFERRED_SIZE 0x13fe8ULL
#define NIKON_SLA_PEER_MEMORY_BASE 0xc5000000ULL
#define NIKON_SLA_PEER_MEMORY_SIZE 0x1100000ULL
#define NIKON_SLA_LOCAL_CODE_BASE 0xc8200000ULL
#define NIKON_SLA_LOCAL_CODE_SIZE 0x700000ULL
#define NIKON_SLA_BOOT_PREFIX_BASE 0x10000000ULL
#define NIKON_SLA_BOOT_PREFIX_SIZE 0x20000ULL

#endif
