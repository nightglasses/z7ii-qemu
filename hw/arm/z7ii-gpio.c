/*
 * Twelve-port Milbeaut-compatible masked GPIO functional interface.
 * Pin count/address and bootstrap table come from original MAS; this is not
 * a physical board wiring or sensor/key matrix reconstruction.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "z7ii.h"

#define GPIO_BASE 0x04282000ULL
#define GPIO_PORTS 12
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIGPIOState, Z7II_GPIO)

struct Z7IIGPIOState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    /* PDR output latch, DDR, EPCR, PUDER, PUDCR. */
    uint16_t ports[5][GPIO_PORTS];
    bool reported_error;
};

static MemTxResult z7ii_gpio_error(Z7IIGPIOState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value,
                                  const char *reason)
{
    z7ii_access_error(&s->reported_error, GPIO_BASE + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static bool z7ii_gpio_decode(hwaddr offset, unsigned size,
                             unsigned *bank, unsigned *port)
{
    *bank = offset >> 8;
    *port = (offset & 0xff) >> 2;
    return size == 4 && !(offset & 3) && *bank < 5 && *port < GPIO_PORTS;
}

static MemTxResult z7ii_gpio_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    Z7IIGPIOState *s = opaque;
    unsigned bank, port;

    *value = 0;
    if (!z7ii_gpio_decode(offset, size, &bank, &port)) {
        return z7ii_gpio_error(s, offset, size, false, 0,
                               "gpio-access-not-supported");
    }
    if (bank == 0) {
        uint16_t outputs = s->ports[1][port] & ~s->ports[2][port];
        uint16_t input = s->ports[3][port] & s->ports[4][port];

        /* Unconnected input/alternate pads follow pull, otherwise virtual low. */
        *value = (s->ports[0][port] & outputs) | (input & ~outputs);
    } else {
        *value = s->ports[bank][port];
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    Z7IIGPIOState *s = opaque;
    unsigned bank, port;

    if (!z7ii_gpio_decode(offset, size, &bank, &port)) {
        return z7ii_gpio_error(s, offset, size, true, value,
                               "gpio-access-not-supported");
    }
    if (bank == 0) {
        uint16_t mask = value >> 16;

        /* Upper16 selects which low16 latch bits change, not stored data. */
        s->ports[0][port] = (s->ports[0][port] & ~mask) | (value & mask);
    } else {
        if (value > 0xffff) {
            return z7ii_gpio_error(s, offset, size, true, value,
                                   "gpio-field-not-supported");
        }
        s->ports[bank][port] = value;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_gpio_ops = {
    .read_with_attrs = z7ii_gpio_read,
    .write_with_attrs = z7ii_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_gpio_reset(DeviceState *dev)
{
    Z7IIGPIOState *s = Z7II_GPIO(dev);

    /* Virtual unconnected GPIO profile: input, no pull, latch low. */
    memset(s->ports, 0, sizeof(s->ports));
    s->reported_error = false;
}

static void z7ii_gpio_init(Object *obj)
{
    Z7IIGPIOState *s = Z7II_GPIO(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_gpio_ops, s, "z7ii.gpio", 0x430);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription z7ii_gpio_vmstate = {
    .name = "z7ii-gpio",
    .unmigratable = true,
};

static void z7ii_gpio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_gpio_vmstate;
    device_class_set_legacy_reset(dc, z7ii_gpio_reset);
    dc->user_creatable = false;
}

static const TypeInfo z7ii_gpio_type = {
    .name = TYPE_Z7II_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIGPIOState),
    .instance_init = z7ii_gpio_init,
    .class_init = z7ii_gpio_class_init,
};

static void z7ii_gpio_register_types(void)
{
    type_register_static(&z7ii_gpio_type);
}
type_init(z7ii_gpio_register_types)
