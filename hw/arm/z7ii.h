/* Z7II diagnostic board/device boundary. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_ARM_Z7II_H
#define HW_ARM_Z7II_H

#include "system/memory.h"
#include "hw/core/qdev.h"

#define TYPE_Z7II_FILL "z7ii-fill-functional"
#define TYPE_Z7II_USIO "z7ii-usio-functional"
#define TYPE_Z7II_SPI "z7ii-spi-functional"
#define TYPE_Z7II_SYSCONFIG "z7ii-sysconfig-functional"
#define TYPE_Z7II_GPIO "z7ii-gpio-functional"
#define TYPE_Z7II_EXIU "z7ii-exiu-functional"
#define TYPE_Z7II_HDMAC "z7ii-hdmac-functional"
#define TYPE_Z7II_I2C "z7ii-i2c-functional"
#define TYPE_Z7II_AUDIO "z7ii-audio-functional"
#define TYPE_Z7II_TIMING "z7ii-timing-functional"
#define TYPE_Z7II_COPY "z7ii-copy-functional"
#define TYPE_Z7II_BRIDGE "z7ii-bridge-functional"
#define TYPE_Z7II_SIGNAL "z7ii-signal-functional"
#define TYPE_Z7II_IPUD "z7ii-ipud-functional"
#define TYPE_Z7II_CONTROL20202 "z7ii-control20202-functional"
#define TYPE_Z7II_CONTROL2010 "z7ii-control2010-functional"
#define TYPE_Z7II_SDI "z7ii-sdi-functional"
#define TYPE_Z7II_MXIC "z7ii-mxic-regions-functional"
#define TYPE_Z7II_MXIC_CONFIG "z7ii-mxic-config-functional"

void z7ii_access_error(bool *reported, hwaddr addr, unsigned size,
                       bool write, uint64_t value, const char *reason);
void z7ii_mxic_attach_ram(DeviceState *dev, MemoryRegion *ram, hwaddr address);

#endif
