/*
 * Source-bound word command windows and arbitration. No byte DMA or physical
 * IP identity is inferred. Peer frames and completions require real providers.
 * Cold state, single outstanding event per slot and acknowledgement side
 * effects are explicit functional conventions, documented with their limits.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/command.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonCommandState, NIKON_COMMAND)

typedef struct NikonCommandEngine {
    NikonCommandRequest request;
    uint16_t causes;
    bool active, received;
} NikonCommandEngine;

typedef struct NikonCommandBank {
    NikonCommandState *controller;
    MemoryRegion regs;
    unsigned index;
    NikonCommandEngine engine[NIKON_COMMAND_ENGINES];
    /* A slot can identify one pending engine; -1 means no event. */
    int pending[16];
} NikonCommandBank;

struct NikonCommandState {
    SysBusDevice parent_obj;
    NikonCommandBank bank[NIKON_COMMAND_BANKS];
    qemu_irq irq[20];
    qemu_irq request[24];
    uint64_t serial;
    bool reported_error;
};

static const unsigned slot_count[] = { 16, 2, 2 };
static const unsigned irq_base[] = { 0, 16, 18 };

static void command_lines(NikonCommandBank *b)
{
    NikonCommandState *s = b->controller;
    unsigned i;

    for (i = 0; i < slot_count[b->index]; i++) {
        qemu_set_irq(s->irq[irq_base[b->index] + i], b->pending[i] >= 0);
    }
    for (i = 0; i < NIKON_COMMAND_ENGINES; i++) {
        qemu_set_irq(s->request[b->index * NIKON_COMMAND_ENGINES + i],
                     b->engine[i].active);
    }
}

static void command_clear(NikonCommandBank *b, unsigned engine, uint16_t slots)
{
    unsigned i;

    for (i = 0; i < slot_count[b->index]; i++) {
        if ((slots & (1U << i)) && b->pending[i] == engine) {
            b->pending[i] = -1;
        }
    }
}

static bool command_indices(unsigned bank, unsigned engine)
{
    return bank < NIKON_COMMAND_BANKS && engine < NIKON_COMMAND_ENGINES;
}

bool nikon_command_request(DeviceState *dev, unsigned bank, unsigned engine,
                           NikonCommandRequest *request)
{
    NikonCommandState *s = NIKON_COMMAND(dev);

    if (!dev->realized || !command_indices(bank, engine) || !request ||
        !s->bank[bank].engine[engine].active) {
        return false;
    }
    *request = s->bank[bank].engine[engine].request;
    return true;
}

bool nikon_command_complete(DeviceState *dev, unsigned bank, unsigned engine,
                            uint64_t token, uint16_t causes)
{
    NikonCommandState *s = NIKON_COMMAND(dev);
    NikonCommandBank *b;
    NikonCommandEngine *e;
    unsigned i;

    if (!dev->realized || !command_indices(bank, engine) || !causes) {
        return false;
    }
    b = &s->bank[bank];
    e = &b->engine[engine];
    if (!e->active || token != e->request.token) {
        return false;
    }
    for (i = 0; i < slot_count[bank]; i++) {
        if ((e->request.slots & (1U << i)) && b->pending[i] >= 0) {
            return false;
        }
    }
    e->active = false;
    e->causes = causes;
    for (i = 0; i < slot_count[bank]; i++) {
        if (e->request.slots & (1U << i)) {
            b->pending[i] = engine;
        }
    }
    command_lines(b);
    return true;
}

bool nikon_command_receive(DeviceState *dev, unsigned bank, unsigned engine,
                           unsigned slot, const uint32_t *words, unsigned count)
{
    NikonCommandState *s = NIKON_COMMAND(dev);
    NikonCommandBank *b;
    NikonCommandEngine *e;

    if (!dev->realized || !command_indices(bank, engine) ||
        slot >= slot_count[bank] || !words || !count ||
        count > NIKON_COMMAND_WORDS) {
        return false;
    }
    b = &s->bank[bank];
    e = &b->engine[engine];
    if (e->request.slots || b->pending[slot] >= 0) {
        return false;
    }
    memset(e, 0, sizeof(*e));
    e->request.slots = 1U << slot;
    memcpy(e->request.words, words, count * sizeof(*words));
    e->request.written = (1U << count) - 1;
    e->received = true;
    b->pending[slot] = engine;
    command_lines(b);
    return true;
}

static MemTxResult command_error(NikonCommandBank *b, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&b->controller->reported_error, b->regs.addr + offset,
                       size, write, value, "command-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult command_read(void *opaque, hwaddr offset, uint64_t *value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonCommandBank *b = opaque;
    NikonCommandEngine *e;
    unsigned engine, port, i;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return command_error(b, offset, size, false, 0);
    }
    if (offset >= 0x80 && offset < 0x80 + slot_count[b->index] * 4) {
        int pending = b->pending[(offset - 0x80) / 4];

        if (pending >= 0) {
            /* Original ISR's TBB maps this encoding to the engine index. */
            *value = (pending + 2) << 7;
        }
        return MEMTX_OK;
    }
    if (offset == 0x900) {
        for (i = 0; i < NIKON_COMMAND_ENGINES; i++) {
            *value |= !!b->engine[i].request.slots << i;
        }
        return MEMTX_OK;
    }
    if (offset < 0x100 || offset >= 0x500) {
        return command_error(b, offset, size, false, 0);
    }
    engine = (offset - 0x100) / 0x80;
    port = (offset - 0x100) % 0x80;
    e = &b->engine[engine];
    switch (port) {
    case 0: *value = e->request.slots; break;
    case 4: *value = e->request.control; break;
    case 8: *value = e->active; break;
    case 0x10: *value = e->request.parameter; break;
    case 0x20: *value = e->request.extension; break;
    case 0x38: *value = e->causes; break;
    default:
        if (port >= 0x40) {
            *value = e->request.words[(port - 0x40) / 4];
        } else {
            return command_error(b, offset, size, false, 0);
        }
    }
    return MEMTX_OK;
}

static MemTxResult command_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonCommandBank *b = opaque;
    NikonCommandEngine *e;
    unsigned engine, port;
    uint16_t mask = (1U << slot_count[b->index]) - 1;

    if (size != 4 || (offset & 3) || offset < 0x100 || offset >= 0x500) {
        return command_error(b, offset, size, true, value);
    }
    engine = (offset - 0x100) / 0x80;
    port = (offset - 0x100) % 0x80;
    e = &b->engine[engine];
    switch (port) {
    case 0:
        if (value & ~mask || (value && (value & (value - 1)))) {
            return command_error(b, offset, size, true, value);
        }
        if (!value) {
            command_clear(b, engine, mask);
            memset(e, 0, sizeof(*e));
        } else if (!e->request.slots) {
            e->request.slots = value;
        }
        /* A lost arbitration retains the actual owner for guest readback. */
        break;
    case 8:
        if (value != 1 || !e->request.slots || e->active || e->received ||
            e->causes || !e->request.written) {
            return command_error(b, offset, size, true, value);
        }
        e->active = true;
        e->request.token = ++b->controller->serial;
        if (!e->request.token) {
            e->request.token = ++b->controller->serial;
        }
        break;
    case 0x14:
        if (value & ~mask || !e->received ||
            !(value & e->request.slots)) {
            return command_error(b, offset, size, true, value);
        }
        command_clear(b, engine, value);
        e->request.slots = 0;
        e->received = false;
        break;
    case 0x34:
        if (value & ~0xffffU || !e->causes || value != e->causes) {
            return command_error(b, offset, size, true, value);
        }
        command_clear(b, engine, e->request.slots);
        e->causes = 0;
        break;
    case 4:
    case 0x10:
    case 0x20:
        if (e->active || e->received || value & ~(port == 4 ? 7U : 0xffffU)) {
            return command_error(b, offset, size, true, value);
        }
        if (port == 4) {
            e->request.control = value;
        } else if (port == 0x10) {
            e->request.parameter = value;
        } else {
            e->request.extension = value;
        }
        break;
    default:
        if (port < 0x40 || e->active || e->received) {
            return command_error(b, offset, size, true, value);
        }
        e->request.words[(port - 0x40) / 4] = value;
        e->request.written |= 1U << ((port - 0x40) / 4);
        break;
    }
    command_lines(b);
    return MEMTX_OK;
}

static const MemoryRegionOps command_ops = {
    .read_with_attrs = command_read,
    .write_with_attrs = command_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void command_reset(DeviceState *dev)
{
    NikonCommandState *s = NIKON_COMMAND(dev);
    unsigned i, j;

    s->reported_error = false;
    for (i = 0; i < NIKON_COMMAND_BANKS; i++) {
        memset(s->bank[i].engine, 0, sizeof(s->bank[i].engine));
        for (j = 0; j < G_N_ELEMENTS(s->bank[i].pending); j++) {
            s->bank[i].pending[j] = -1;
        }
        command_lines(&s->bank[i]);
    }
}

static void command_init(Object *obj)
{
    NikonCommandState *s = NIKON_COMMAND(obj);
    unsigned i;

    for (i = 0; i < NIKON_COMMAND_BANKS; i++) {
        g_autofree char *name = g_strdup_printf("nikon.command.bank%u", i);
        NikonCommandBank *b = &s->bank[i];

        b->controller = s;
        b->index = i;
        memory_region_init_io(&b->regs, obj, &command_ops, b, name, 0x904);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &b->regs);
    }
    for (i = 0; i < G_N_ELEMENTS(s->irq); i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
    qdev_init_gpio_out_named(DEVICE(obj), s->request, "request", 24);
    command_reset(DEVICE(obj));
}

static const VMStateDescription command_vmstate = {
    .name = TYPE_NIKON_COMMAND, .unmigratable = true,
};

static void command_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &command_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, command_reset);
    dc->unrealize = command_reset;
}

static const TypeInfo command_type = {
    .name = TYPE_NIKON_COMMAND,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCommandState),
    .instance_init = command_init,
    .class_init = command_class_init,
};

static void command_register_types(void)
{
    type_register_static(&command_type);
}
type_init(command_register_types)
