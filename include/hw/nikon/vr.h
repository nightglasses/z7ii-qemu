/* Nikon VR RX peripheral protocol. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_VR_H
#define HW_NIKON_VR_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"

#define TYPE_NIKON_VR_PERIPHERALS "nikon-vr-peripherals"

/* pclk_hz is an explicit functional input clock, not a silicon identification. */
DeviceState *nikon_vr_peripherals_create(Object *owner, MemoryRegion *memory,
                                       qemu_irq irq, qemu_irq fir,
                                       uint64_t pclk_hz);
qemu_irq nikon_vr_peripherals_ack(DeviceState *dev);
DeviceState *nikon_vr_peripherals_attach_spi(DeviceState *dev, SSIBus *bus);

#endif
