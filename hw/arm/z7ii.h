/* Z7II diagnostic board/device boundary. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_ARM_Z7II_H
#define HW_ARM_Z7II_H

#include "system/memory.h"
#include "hw/core/qdev.h"

#define TYPE_Z7II_FILL "z7ii-fill-functional"
#define TYPE_Z7II_USIO "z7ii-usio-functional"
#define TYPE_Z7II_SYSCONFIG "z7ii-sysconfig-functional"
#define TYPE_Z7II_GPIO "z7ii-gpio-functional"
#define TYPE_Z7II_SDI "z7ii-sdi-functional"
#define TYPE_Z7II_MXIC "z7ii-mxic-regions-functional"

void z7ii_access_error(bool *reported, hwaddr addr, unsigned size,
                       bool write, uint64_t value, const char *reason);
void z7ii_mxic_attach_ram(DeviceState *dev, MemoryRegion *ram, hwaddr address);

#endif
