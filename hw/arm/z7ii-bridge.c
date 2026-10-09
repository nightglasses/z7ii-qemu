/*
 * Finite MAS 2013 control/bridge protocol, not an identified Nikon IP.
 * Original bounded-source contract23d2601: init/request/wait/ISR and the
 * five translation arrays. Reset, 10us timing and internal IRQ causes below
 * are explicit virtual conventions; no peer data/event1 is synthesized.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IIBridgeState, Z7II_BRIDGE)

typedef struct Z7IIBridgeBank {
    QEMUTimer *timer;
    uint32_t request;
    uint32_t pending;
    uint32_t lane_status[8];
    uint32_t context[8][15];
    uint32_t selector;
    uint32_t window[4][3];
    uint32_t configuration[15];
    uint32_t mapping[40];
} Z7IIBridgeBank;

struct Z7IIBridgeState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Clock *clock;
    qemu_irq irq;
    QEMUTimer *control_timer;
    QEMUTimer *configuration_timer;
    Z7IIBridgeBank bank[2];
    uint32_t reset;
    uint32_t routing[2];
    uint32_t command;
    uint32_t ready;
    uint32_t irq_status;
    uint32_t irq_enable;
    bool configuration_pending;
    bool reported_error;
};

/* Exact currently recovered configuration profile, not silicon field limits. */
static const struct {
    unsigned offset;
    uint32_t value;
} context_profile[] = {
    { 0x84, 2 }, { 0x88, 2 }, { 0x8c, 3 }, { 0x90, 1 },
    { 0xa0, 0x48 }, { 0xa4, 5 }, { 0xa8, 4 }, { 0xb0, 0 },
    { 0xb4, 0x20 }, { 0xbc, 1 }, { 0xcc, 8 }, { 0xdc, 0 },
    { 0xe0, 0xf }, { 0xe4, 0x36 }, { 0xac, 0 },
};
static const struct {
    unsigned offset;
    uint32_t value;
} bank_profile[] = {
    { 0x4104, 0x78 }, { 0x412c, 0xff }, { 0x4130, 0x2ee0 },
    { 0x4134, 0xa }, { 0x4140, 1 }, { 0x4400, 0x37 },
    { 0x4504, 0xd4d }, { 0x450c, 0x128 }, { 0x4520, 0x1b },
    { 0x45fc, 0 }, { 0x5004, 0x10cc3200 }, { 0x504c, 0x11f },
    { 0x4120, 4 }, { 0x4124, 4 }, { 0x4910, 4 },
};

static void z7ii_bridge_irq(Z7IIBridgeState *s)
{
    qemu_set_irq(s->irq, !!(s->irq_status & s->irq_enable));
}

static int64_t z7ii_bridge_deadline(Z7IIBridgeState *s)
{
    /* Independent 48 MHz, 480 ticks = 10 us is a virtual apply latency. */
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
           clock_ticks_to_ns(s->clock, 480);
}

static void z7ii_bridge_lanes_done(void *opaque)
{
    Z7IIBridgeBank *b = opaque;
    unsigned i;

    for (i = 0; i < 8; i++) {
        if (b->pending & (1U << i)) {
            b->lane_status[(i & 3) * 2 + (i >= 4)] = 3;
        }
    }
    b->pending = 0;
    /* Lane status completion does not fabricate the peer's event1/IRQbit0. */
}

static void z7ii_bridge_control_done(void *opaque)
{
    Z7IIBridgeState *s = opaque;

    if (s->command == 6) {
        s->ready = 1;
    }
    s->irq_status |= 2;
    z7ii_bridge_irq(s);
}

static void z7ii_bridge_configuration_done(void *opaque)
{
    Z7IIBridgeState *s = opaque;

    /* Command2 precedes array publication in the original caller. */
    s->configuration_pending = false;
    s->irq_status |= 0x40;
    z7ii_bridge_irq(s);
}

static void z7ii_bridge_cancel(Z7IIBridgeState *s)
{
    unsigned bank;

    timer_del(s->control_timer);
    timer_del(s->configuration_timer);
    s->command = s->ready = s->irq_status = 0;
    s->configuration_pending = false;
    for (bank = 0; bank < 2; bank++) {
        Z7IIBridgeBank *b = &s->bank[bank];

        timer_del(b->timer);
        b->pending = b->request = b->selector = 0;
        memset(b->lane_status, 0, sizeof(b->lane_status));
        memset(b->context, 0, sizeof(b->context));
        memset(b->window, 0, sizeof(b->window));
        memset(b->configuration, 0, sizeof(b->configuration));
        memset(b->mapping, 0, sizeof(b->mapping));
    }
    z7ii_bridge_irq(s);
}

static int z7ii_bridge_field(unsigned offset, uint32_t *profile)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(context_profile); i++) {
        if (context_profile[i].offset == offset) {
            *profile = context_profile[i].value;
            return i;
        }
    }
    return -1;
}

static MemTxResult z7ii_bridge_error(Z7IIBridgeState *s, hwaddr offset,
                                    unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, 0x20130000 + offset, size, write,
                      value, "bridge-access-or-protocol-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_bridge_access(Z7IIBridgeState *s, hwaddr offset,
                                     uint64_t *value, unsigned size, bool write)
{
    Z7IIBridgeBank *b = &s->bank[offset >= 0x8000];
    unsigned local = offset & 0x7fff;
    unsigned context, reg, i;
    uint32_t profile;
    int field;

    if (size != 4 || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 0xc5e0:
        if (write) {
            if (*value != 0 && *value != 0xf000f) {
                goto error;
            }
            if (*value) {
                z7ii_bridge_cancel(s);
            }
            s->reset = *value;
        } else {
            *value = s->reset;
        }
        return MEMTX_OK;
    case 0xc5f4:
    case 0xc5f8:
        i = (offset - 0xc5f4) / 4;
        if (write) {
            if (*value != 0x1b && *value != 0xe4) {
                goto error;
            }
            s->routing[i] = *value;
        } else {
            *value = s->routing[i];
        }
        return MEMTX_OK;
    case 0x4908:
        if (write) {
            goto error;
        }
        *value = s->ready;
        return MEMTX_OK;
    case 0xc90c:
        if (write) {
            if ((*value != 4 && *value != 6) || s->reset ||
                timer_pending(s->control_timer) || !s->routing[0] ||
                s->routing[0] != s->routing[1] ||
                !s->bank[1].request ||
                s->bank[1].pending) {
                goto error;
            }
            s->command = *value;
            if (*value == 6) {
                s->ready = 0;
            }
            timer_mod(s->control_timer, z7ii_bridge_deadline(s));
        } else {
            *value = s->command;
        }
        return MEMTX_OK;
    case 0xd020:
        if (write) {
            goto error;
        }
        *value = s->irq_status;
        return MEMTX_OK;
    case 0xd024:
        if (write) {
            if (*value & ~0xffffU) {
                goto error;
            }
            s->irq_enable = *value;
            z7ii_bridge_irq(s);
        } else {
            *value = s->irq_enable;
        }
        return MEMTX_OK;
    case 0xd028:
        if (!write || (*value & ~0xffffU)) {
            goto error;
        }
        s->irq_status &= ~*value;
        z7ii_bridge_irq(s);
        return MEMTX_OK;
    }

    if (local < 0x1000) {
        context = (local / 0x400) * 2 + ((local & 0x200) != 0);
        reg = local & 0x1ff;
        if (reg == 0x104) {
            if (write) {
                goto error;
            }
            *value = b->lane_status[context];
            return MEMTX_OK;
        }
        /* One selected context's three configuration-window words. */
        if (local == 0x1c8 || local == 0x1cc || local == 0x1ec) {
            i = local == 0x1c8 ? 0 : local == 0x1cc ? 1 : 2;
            profile = i == 0 ? 0x44 : i == 1 ? 0x1c : 0x20;
            if (!b->selector) {
                goto error;
            }
            context = ctz32(b->selector >> 16);
            if (write) {
                if (*value != profile || s->reset || b->pending) {
                    goto error;
                }
                b->window[context][i] = *value;
            } else {
                *value = b->window[context][i];
            }
            return MEMTX_OK;
        }
        field = z7ii_bridge_field(reg, &profile);
        if (field < 0 || ((local & 0x200) && reg != 0x84 && reg != 0x88 &&
                         reg != 0x8c && reg != 0x90)) {
            goto error;
        }
        if (write) {
            if ((reg == 0xac ? *value > 1 : *value != profile) ||
                s->reset || b->pending) {
                goto error;
            }
            b->context[context][field] = *value;
        } else {
            *value = b->context[context][field];
        }
        return MEMTX_OK;
    }

    if (local == 0x45f0) {
        if (write) {
            if (s->reset || b->pending || (*value &&
                          ((*value & ~0xf0000U) ||
                           !is_power_of_2(*value)))) {
                goto error;
            }
            b->selector = *value;
        } else {
            *value = b->selector;
        }
        return MEMTX_OK;
    }
    if (local == 0x4128) {
        if (write) {
            unsigned selected = (*value & 0xf) | ((*value >> 8) & 0xf0);

            if (!selected || (*value & ~0xf00fU) || s->reset || b->pending) {
                goto error;
            }
            for (i = 0; i < 8; i++) {
                if (selected & (1U << i)) {
                    unsigned c = (i & 3) * 2 + (i >= 4);

                    if (b->context[c][1] != 2 || b->context[c][2] != 3) {
                        goto error;
                    }
                }
            }
            b->request = *value;
            b->pending = selected;
            for (i = 0; i < 8; i++) {
                if (selected & (1U << i)) {
                    unsigned c = (i & 3) * 2 + (i >= 4);

                    b->lane_status[c] = 1;
                }
            }
            timer_mod(b->timer, z7ii_bridge_deadline(s));
        } else {
            *value = b->request;
        }
        return MEMTX_OK;
    }
    if (local >= 0x50b0 && local < 0x5150) {
        i = (local - 0x50b0) / 4;
        if (write) {
            if ((*value & ~0xfffffU) || (i >= 24 && *value) ||
                s->reset || s->configuration_pending) {
                goto error;
            }
            b->mapping[i] = *value;
        } else {
            *value = b->mapping[i];
        }
        return MEMTX_OK;
    }
    if (local == 0x413c) {
        if (!write || *value != 2 || s->reset || s->configuration_pending ||
            !s->bank[1].request || s->bank[1].pending) {
            goto error;
        }
        s->configuration_pending = true;
        timer_mod(s->configuration_timer, z7ii_bridge_deadline(s));
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(bank_profile); i++) {
        if (local == bank_profile[i].offset) {
            if (write) {
                if (*value != bank_profile[i].value || s->reset || b->pending) {
                    goto error;
                }
                b->configuration[i] = *value;
            } else {
                *value = b->configuration[i];
            }
            return MEMTX_OK;
        }
    }
error:
    return z7ii_bridge_error(s, offset, size, write, write ? *value : 0);
}

static MemTxResult z7ii_bridge_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    *value = 0;
    return z7ii_bridge_access(opaque, offset, value, size, false);
}

static MemTxResult z7ii_bridge_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    return z7ii_bridge_access(opaque, offset, &value, size, true);
}

static const MemoryRegionOps z7ii_bridge_ops = {
    .read_with_attrs = z7ii_bridge_read,
    .write_with_attrs = z7ii_bridge_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_bridge_reset(DeviceState *dev)
{
    Z7IIBridgeState *s = Z7II_BRIDGE(dev);

    s->reset = s->routing[0] = s->routing[1] = s->irq_enable = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    z7ii_bridge_cancel(s);
}

static void z7ii_bridge_init(Object *obj)
{
    Z7IIBridgeState *s = Z7II_BRIDGE(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-bridge-clock");
    s->control_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    z7ii_bridge_control_done, s);
    s->configuration_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                         z7ii_bridge_configuration_done, s);
    for (i = 0; i < 2; i++) {
        s->bank[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       z7ii_bridge_lanes_done, &s->bank[i]);
    }
    memory_region_init_io(&s->regs, obj, &z7ii_bridge_ops, s,
                          "z7ii.bridge", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void z7ii_bridge_finalize(Object *obj)
{
    Z7IIBridgeState *s = Z7II_BRIDGE(obj);
    unsigned i;

    timer_free(s->control_timer);
    timer_free(s->configuration_timer);
    for (i = 0; i < 2; i++) {
        timer_free(s->bank[i].timer);
    }
}

static const VMStateDescription z7ii_bridge_vmstate = {
    .name = "z7ii-bridge", .unmigratable = true,
};

static void z7ii_bridge_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_bridge_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_bridge_reset);
}

static const TypeInfo z7ii_bridge_type = {
    .name = TYPE_Z7II_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIBridgeState),
    .instance_init = z7ii_bridge_init,
    .instance_finalize = z7ii_bridge_finalize,
    .class_init = z7ii_bridge_class_init,
};

static void z7ii_bridge_register_types(void)
{
    type_register_static(&z7ii_bridge_type);
}
type_init(z7ii_bridge_register_types)
