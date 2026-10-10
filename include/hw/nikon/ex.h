/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_EX_H
#define HW_NIKON_EX_H

#include "hw/core/sysbus.h"
#include "hw/i2c/i2c.h"
#include "hw/ssi/ssi.h"

#define TYPE_NIKON_EX "nikon-ex"
OBJECT_DECLARE_SIMPLE_TYPE(NikonEXState, NIKON_EX)

/* Set the complete immutable component before realizing the processor. */
bool nikon_ex_set_firmware(NikonEXState *s, GBytes *component, Error **errp);
MemoryRegion *nikon_ex_memory(NikonEXState *s);
I2CBus *nikon_ex_i2c_bus(NikonEXState *s, unsigned channel);
/* Realized 24-pad external I2C peer. Named pad inputs accept 0/1/-1. */
DeviceState *nikon_ex_io(NikonEXState *s);
SSIBus *nikon_ex_spi_bus(NikonEXState *s, unsigned channel);
/* A real external SPI clock, when EX has configured this channel as slave. */
uint32_t nikon_ex_spi_transfer(NikonEXState *s, unsigned channel,
                             uint32_t value);
/* Realized slave attached to the caller's source-proven master bus.
 * EX channel is zero-based: original MCU RX is 0 (SPI1), TX is 3 (SPI4).
 * The master bus owns the returned device; NULL reports realization failure.
 * The MCU peer profile requires explicit 16-bit master words and wire bit order.
 * An 8-bit EX channel receives two sequential clocks per master word.
 */
DeviceState *nikon_ex_spi_slave(NikonEXState *s, unsigned channel, SSIBus *bus,
                                uint8_t chip_select, unsigned master_bits,
                                bool master_lsb_first, Error **errp);
/*
 * Stable physical input: 0/1 drive, -1 release. Driven levels survive MCU and
 * GPIO RCC reset; only the source peer changes its electrical state.
 */
qemu_irq nikon_ex_gpio_input(NikonEXState *s, unsigned port, unsigned pin);
void nikon_ex_gpio_connect_output(NikonEXState *s, unsigned port, unsigned pin,
                                  qemu_irq sink);
/*
 * Held raw 12-bit sample: 0..4095 supplies a count, -1 disconnects the source.
 * Named "adc" input index is adc * 19 + channel, with the same semantics.
 * MCU reset clears conversion state, preserving externally supplied samples.
 * May be set after object creation, before realization; no conversion is
 * scheduled until realized hardware receives a guest conversion request.
 * No physical voltage, default sample or conversion clock is inferred.
 */
void nikon_ex_adc_input(NikonEXState *s, unsigned adc, unsigned channel,
                        int sample);

#endif
