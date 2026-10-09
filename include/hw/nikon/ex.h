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
SSIBus *nikon_ex_spi_bus(NikonEXState *s, unsigned channel);
/* A real external SPI clock, when EX has configured this channel as slave. */
uint32_t nikon_ex_spi_transfer(NikonEXState *s, unsigned channel,
                             uint32_t value);
qemu_irq nikon_ex_gpio_input(NikonEXState *s, unsigned port, unsigned pin);
/* Real external analog sample. No samples are fabricated at reset. */
void nikon_ex_adc_input(NikonEXState *s, unsigned adc, unsigned channel,
                        uint16_t sample);

#endif
