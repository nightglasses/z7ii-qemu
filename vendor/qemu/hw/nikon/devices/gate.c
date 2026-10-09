/*
 * Finite original 2010 configuration, masks and external pending inputs.
 * Cold zero, retained configuration readback, rising input latches, mask1
 * enable, external gate, deferred stop ACK and W1C are virtual laws, not pipeline
 * semantics.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/timer.h"
#include "hw/nikon/devices.h"

#define CONTROL_SIZE 0x2928
OBJECT_DECLARE_SIMPLE_TYPE(NikonEventGateState, NIKON_EVENT_GATE)

struct NikonEventGateState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[CONTROL_SIZE / 4];
    uint32_t mask[3], pending[3], inputs[3];
    QEMUTimer *stop_timer;
    bool stopping, stop_ack;
    bool reported_error;
};

/* Finite setup shapes, not a physical writable-bit specification. */
static bool nikon_event_gate_config(hwaddr offset, uint32_t *mask)
{
    hwaddr local;

    *mask = UINT32_MAX;
    if (offset == 0x30 || offset == 0x34) {
        /* Whole runtime RMW accepts an arbitrary 32-bit OR operand. */
        return true;
    }
    if (offset == 0xf4 || offset == 0x108) {
        *mask = 0x000fff00;
        return true;
    }
    if ((offset >= 0xf8 && offset <= 0x104) ||
        (offset >= 0x120 && offset <= 0x12c)) {
        *mask = 0xfffffff0;
        return true;
    }
    if (offset == 0x10c) {
        *mask = 0xff0fff00;
        return true;
    }
    if ((offset >= 0x2200 && offset <= 0x22fc) ||
        (offset >= 0x2300 && offset <= 0x23fc) ||
        (offset >= 0x2880 && offset <= 0x28f4) ||
        (offset >= 0x2900 && offset <= 0x2924) || offset == 0x2684) {
        return true;
    }
    if (offset >= 0x2820 && offset <= 0x2838) {
        /* Setter uses whole bytes; getter's 2/2/4 fields are not a mask. */
        *mask = offset == 0x2838 ? 0xffff : UINT32_MAX;
        return true;
    }
    if ((offset >= 0x2000 && offset <= 0x2018) ||
        (offset >= 0x2100 && offset <= 0x2118)) {
        local = offset & 0xff;
        switch (local) {
        case 0:
            /* Dynamic RMW retains arbitrary high configuration bits. */
            return true;
        case 4:
            *mask = 0x3fff;
            return true;
        case 8:
            *mask = 0xff;
            return true;
        case 0x10: case 0x14:
            return true;
        case 0x18:
            *mask = 0xffff;
            return true;
        }
    }
    if ((offset >= 0x2600 && offset <= 0x2618) ||
        (offset >= 0x2640 && offset <= 0x2658)) {
        local = offset & 0x3f;
        switch (local) {
        case 0:
            *mask = 0xffff;
            return true;
        case 4: case 8:
            return true;
        case 0xc: case 0x18:
            *mask = 0xff;
            return true;
        case 0x10:
            *mask = 0xffff00ff;
            return true;
        }
    }
    if (offset == 0x2690) {
        *mask = 0xffff;
        return true;
    }
    return false;
}

static void nikon_event_gate_irq(NikonEventGateState *s)
{
    bool external = (s->config[1] & 3) == 1 &&
                    !!((s->pending[0] & s->mask[0]) |
                       (s->pending[1] & s->mask[1]) |
                       (s->pending[2] & s->mask[2]));
    bool internal = s->stop_ack && (s->pending[0] & s->mask[0] & 1);

    qemu_set_irq(s->irq, external || internal);
}

static void nikon_event_gate_stop_done(void *opaque)
{
    NikonEventGateState *s = opaque;

    s->stopping = false;
    s->stop_ack = true;
    s->pending[0] |= 1;
    nikon_event_gate_irq(s);
}

static void nikon_event_gate_input(void *opaque, int index, int level)
{
    NikonEventGateState *s = opaque;
    unsigned word = index / 32;
    uint32_t bit = 1U << (index % 32);
    bool rising = level && !(s->inputs[word] & bit);

    /* Explicit fixture bits, not names or producers for physical events. */
    s->inputs[word] = (s->inputs[word] & ~bit) | (level ? bit : 0);
    if (rising && (s->config[1] & 3) == 1) {
        s->pending[word] |= bit;
        nikon_event_gate_irq(s);
    }
}

static MemTxResult nikon_event_gate_error(NikonEventGateState *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value,
                      "event-gate-access-or-command-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_event_gate_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonEventGateState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_event_gate_error(s, offset, size, false, 0);
    }
    if (nikon_event_gate_config(offset, &mask)) {
        /* Retained setup values, never unbound data/monitor word defaults. */
        *value = s->config[offset / 4];
        return MEMTX_OK;
    }
    if (offset >= 0xc && offset <= 0x14) {
        *value = s->mask[(offset - 0xc) / 4];
    } else if (offset >= 0x18 && offset <= 0x20) {
        *value = s->pending[(offset - 0x18) / 4];
    } else if (offset == 4) {
        *value = s->config[1];
    } else {
        return nikon_event_gate_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_event_gate_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonEventGateState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        return nikon_event_gate_error(s, offset, size, true, value);
    }
    if (nikon_event_gate_config(offset, &mask)) {
        if ((value & ~mask) ||
            ((offset == 0x2000 || offset == 0x2100) && (value & 0xf) > 1)) {
            /* Independent input enable configuration has no event effect. */
            return nikon_event_gate_error(s, offset, size, true, value);
        }
        s->config[offset / 4] = value;
        return MEMTX_OK;
    }
    if (offset >= 0xc && offset <= 0x14) {
        s->mask[(offset - 0xc) / 4] = value;
    } else if (offset >= 0x18 && offset <= 0x20) {
        s->pending[(offset - 0x18) / 4] &= ~value;
        if (offset == 0x18 && (value & 1)) {
            s->stop_ack = false;
        }
    } else if (offset == 4) {
        /* Same retained word for RMW; unknown high-field changes reject. */
        if ((value & 3) > 1 ||
            (value && ((value ^ s->config[1]) & ~3U))) {
            return nikon_event_gate_error(s, offset, size, true, value);
        }
        s->config[1] = value;
        if ((value & 3) == 1) {
            /* Reopen cancels an unfinished drain, not its latched ACK. */
            timer_del(s->stop_timer);
            s->stopping = false;
        } else if (!s->stopping && !s->stop_ack) {
            /* One outstanding stop; 20us is nominal virtual drain latency. */
            s->stopping = true;
            timer_mod(s->stop_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 20000);
        }
    } else if (offset == 0 || offset == 8 ||
               offset == 0x20a4 || offset == 0x2620 || offset == 0x2660) {
        /*
         * Only observed inactive/deasserted control. Unknown activation
         * or reset pulses cannot clear fields, pending or invent completion.
         */
        if (value != 0) {
            return nikon_event_gate_error(s, offset, size, true, value);
        }
        s->config[offset / 4] = 0;
    } else {
        /* Includes 2680/2800 requests and all unbound data/monitor words. */
        return nikon_event_gate_error(s, offset, size, true, value);
    }
    nikon_event_gate_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_event_gate_ops = {
    .read_with_attrs = nikon_event_gate_read,
    .write_with_attrs = nikon_event_gate_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_event_gate_reset(DeviceState *dev)
{
    NikonEventGateState *s = NIKON_EVENT_GATE(dev);

    timer_del(s->stop_timer);
    s->stopping = false;
    s->stop_ack = false;
    memset(s->config, 0, sizeof(s->config));
    memset(s->mask, 0, sizeof(s->mask));
    memset(s->pending, 0, sizeof(s->pending));
    s->reported_error = false;
    /* Keep provider levels, so reset/configuration cannot synthesize edges. */
    nikon_event_gate_irq(s);
}

static void nikon_event_gate_init(Object *obj)
{
    NikonEventGateState *s = NIKON_EVENT_GATE(obj);

    s->stop_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 nikon_event_gate_stop_done, s);
    memory_region_init_io(&s->regs, obj, &nikon_event_gate_ops, s,
                          "nikon.event-gate", CONTROL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_event_gate_input, "pending-bit", 96);
}

static void nikon_event_gate_finalize(Object *obj)
{
    timer_free(NIKON_EVENT_GATE(obj)->stop_timer);
}

static const VMStateDescription nikon_event_gate_vmstate = {
    .name = "nikon-event-gate",
    .unmigratable = true,
};

static void nikon_event_gate_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_event_gate_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_event_gate_reset);
}

static const TypeInfo nikon_event_gate_type = {
    .name = TYPE_NIKON_EVENT_GATE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonEventGateState),
    .instance_init = nikon_event_gate_init,
    .instance_finalize = nikon_event_gate_finalize,
    .class_init = nikon_event_gate_class_init,
};

static void nikon_event_gate_register_types(void)
{
    type_register_static(&nikon_event_gate_type);
}
type_init(nikon_event_gate_register_types)
