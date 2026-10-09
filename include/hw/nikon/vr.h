/* Nikon VR RX peripheral protocol. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_VR_H
#define HW_NIKON_VR_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"

#define TYPE_NIKON_VR_PERIPHERALS "nikon-vr-peripherals"
#define TYPE_NIKON_VR_PEER "nikon-vr-peer"

typedef void (*NikonVRClockComplete)(void *, uint64_t token, uint32_t word);
typedef bool (*NikonVRGPIOOutput)(void *, unsigned pin, bool level);

/* pclk_hz is an explicit functional input clock, not a silicon identification. */
DeviceState *nikon_vr_peripherals_create(Object *owner, MemoryRegion *memory,
                                       qemu_irq irq, qemu_irq fir,
                                       uint64_t pclk_hz);
qemu_irq nikon_vr_peripherals_ack(DeviceState *dev);
DeviceState *nikon_vr_peripherals_attach_spi(DeviceState *dev, SSIBus *bus);

/* One external SCI5 byte clock may wait for the actual guest ISR/DMA/TDR.
 * Cancellation prevents a reply; an already clocked frame is not rolled back.
 */
bool nikon_vr_peripherals_clock(DeviceState *, uint64_t token, uint32_t word,
                                unsigned bits, NikonVRClockComplete, void *);
void nikon_vr_peripherals_cancel(DeviceState *, uint64_t token);
void nikon_vr_peripherals_select(DeviceState *, bool level);
bool nikon_vr_peripherals_gpio(DeviceState *, unsigned pin, bool level);
/* Reports configured GPIO output levels. Pin ids are port * 8 + bit. */
bool nikon_vr_peripherals_gpio_out(DeviceState *, NikonVRGPIOOutput, void *);
void nikon_vr_peripherals_gpio_retry(DeviceState *);

#endif
