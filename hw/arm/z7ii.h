/* Z7II diagnostic board/device boundary. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_ARM_Z7II_H
#define HW_ARM_Z7II_H

#include "system/memory.h"

#define TYPE_Z7II_SDI "z7ii-sdi-functional"

void z7ii_access_error(bool *reported, hwaddr addr, unsigned size,
                       bool write, uint64_t value, const char *reason);

#endif
