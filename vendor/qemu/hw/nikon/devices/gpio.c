/*
 * Twelve-port Milbeaut-compatible masked GPIO functional interface.
 * Pin count/address and bootstrap table come from original MAS; this is not
 * a physical board wiring or sensor/key matrix reconstruction.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

#define GPIO_PORTS 12
OBJECT_DECLARE_SIMPLE_TYPE(NikonGPIOState, NIKON_GPIO)

struct NikonGPIOState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    /* PDR output latch, DDR, EPCR, PUDER, PUDCR. */
    uint16_t ports[5][GPIO_PORTS];
    qemu_irq pads[GPIO_PORTS * 16];
    bool reported_error;
};

static uint16_t nikon_gpio_levels(NikonGPIOState *s, unsigned port)
{
    uint16_t outputs = s->ports[1][port] & ~s->ports[2][port];
    uint16_t input = s->ports[3][port] & s->ports[4][port];

    return (s->ports[0][port] & outputs) | (input & ~outputs);
}

static void nikon_gpio_update(NikonGPIOState *s, unsigned port)
{
    uint16_t levels = nikon_gpio_levels(s, port);
    unsigned pin;

    for (pin = 0; pin < 16; pin++) {
        qemu_set_irq(s->pads[port * 16 + pin], !!(levels & (1U << pin)));
    }
}

static MemTxResult nikon_gpio_error(NikonGPIOState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value,
                                  const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static bool nikon_gpio_decode(hwaddr offset, unsigned size,
                             unsigned *bank, unsigned *port)
{
    *bank = offset >> 8;
    *port = (offset & 0xff) >> 2;
    return size == 4 && !(offset & 3) && *bank < 5 && *port < GPIO_PORTS;
}

static MemTxResult nikon_gpio_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonGPIOState *s = opaque;
    unsigned bank, port;

    *value = 0;
    if (!nikon_gpio_decode(offset, size, &bank, &port)) {
        return nikon_gpio_error(s, offset, size, false, 0,
                               "gpio-access-not-supported");
    }
    if (bank == 0) {
        /* Unconnected input/alternate pads follow pull, otherwise virtual low. */
        *value = nikon_gpio_levels(s, port);
    } else {
        *value = s->ports[bank][port];
    }
    return MEMTX_OK;
}

static MemTxResult nikon_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    NikonGPIOState *s = opaque;
    unsigned bank, port;

    if (!nikon_gpio_decode(offset, size, &bank, &port)) {
        return nikon_gpio_error(s, offset, size, true, value,
                               "gpio-access-not-supported");
    }
    if (bank == 0) {
        uint16_t mask = value >> 16;

        /* Upper16 selects which low16 latch bits change, not stored data. */
        s->ports[0][port] = (s->ports[0][port] & ~mask) | (value & mask);
    } else {
        if (value > 0xffff) {
            return nikon_gpio_error(s, offset, size, true, value,
                                   "gpio-field-not-supported");
        }
        s->ports[bank][port] = value;
    }
    nikon_gpio_update(s, port);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_gpio_ops = {
    .read_with_attrs = nikon_gpio_read,
    .write_with_attrs = nikon_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_gpio_reset(DeviceState *dev)
{
    NikonGPIOState *s = NIKON_GPIO(dev);
    unsigned port;

    /* Virtual unconnected GPIO profile: input, no pull, latch low. */
    memset(s->ports, 0, sizeof(s->ports));
    s->reported_error = false;
    for (port = 0; port < GPIO_PORTS; port++) {
        nikon_gpio_update(s, port);
    }
}

static void nikon_gpio_init(Object *obj)
{
    NikonGPIOState *s = NIKON_GPIO(obj);

    memory_region_init_io(&s->regs, obj, &nikon_gpio_ops, s, "nikon.gpio", 0x430);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    qdev_init_gpio_out_named(DEVICE(obj), s->pads, "pad", GPIO_PORTS * 16);
}

static const VMStateDescription nikon_gpio_vmstate = {
    .name = "nikon-gpio",
    .unmigratable = true,
};

static void nikon_gpio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_gpio_vmstate;
    device_class_set_legacy_reset(dc, nikon_gpio_reset);
    dc->user_creatable = false;
}

static const TypeInfo nikon_gpio_type = {
    .name = TYPE_NIKON_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonGPIOState),
    .instance_init = nikon_gpio_init,
    .class_init = nikon_gpio_class_init,
};

static void nikon_gpio_register_types(void)
{
    type_register_static(&nikon_gpio_type);
}
type_init(nikon_gpio_register_types)
