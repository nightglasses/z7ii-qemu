/*
 * Nikon graphics packet transfer.  Register production and the IRQ130 ACK
 * are joined to C1.70 106ee148 and 106ee3d6 respectively.  The byte-stream
 * command is produced by 106eeb5c with control.bit3 and a byte extent.
 * Zero reset and a 48MHz transfer clock are functional-board assumptions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonTransferState, NIKON_TRANSFER)

enum {
    TRANSFER_SOURCE, TRANSFER_DESTINATION, TRANSFER_SOURCE_GEOMETRY,
    TRANSFER_DESTINATION_GEOMETRY, TRANSFER_CONTROL, TRANSFER_START,
    TRANSFER_EXTENT, TRANSFER_EXTENSION, TRANSFER_PADDING,
    TRANSFER_SECONDARY_SOURCE, TRANSFER_SECONDARY_DESTINATION,
    TRANSFER_REGISTER_COUNT,
};

static const hwaddr register_offsets[TRANSFER_REGISTER_COUNT] = {
    0xc00, 0xc04, 0xc08, 0xc0c, 0xc10, 0xc18,
    0xc20, 0xc24, 0xc28, 0xc34, 0xc38,
};

struct NikonTransferState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Clock *clock;
    QEMUTimer *timer;
    qemu_irq irq[3];
    uint32_t reg[TRANSFER_REGISTER_COUNT];
    uint32_t configured;
    uint64_t source, destination, remaining;
    bool running, completed, reported_error;
};

static void nikon_transfer_irq(NikonTransferState *s)
{
    /* IRQ129/131 belong to other producers; they are never invented here. */
    qemu_set_irq(s->irq[0], 0);
    qemu_set_irq(s->irq[1], s->completed);
    qemu_set_irq(s->irq[2], 0);
}

static MemTxResult nikon_transfer_error(NikonTransferState *s, hwaddr offset,
                                       unsigned size, bool write,
                                       uint64_t value, const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static bool nikon_transfer_range(hwaddr address, uint64_t length, bool write)
{
    while (length) {
        MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                        address, length);
        uint64_t span;
        bool direct;

        if (!section.mr) {
            return false;
        }
        span = int128_get64(section.size);
        direct = span && section.offset_within_address_space == address &&
                 memory_access_is_direct(section.mr, write,
                                         MEMTXATTRS_UNSPECIFIED) &&
                 (!write || !section.readonly);
        memory_region_unref(section.mr);
        if (!direct) {
            return false;
        }
        address += span;
        length -= span;
    }
    return true;
}

static void nikon_transfer_schedule(NikonTransferState *s)
{
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              clock_ticks_to_ns(s->clock, MIN(s->remaining, 4096)) + 1);
}

static void nikon_transfer_step(void *opaque)
{
    NikonTransferState *s = opaque;
    uint8_t buffer[4096];
    unsigned length = MIN(s->remaining, sizeof(buffer));
    MemTxResult result;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    if (!s->running) {
        return;
    }
    result = dma_memory_read(&address_space_memory, s->source, buffer,
                             length, attrs);
    if (result == MEMTX_OK) {
        attrs.memory = true;
        result = dma_memory_write(&address_space_memory, s->destination,
                                  buffer, length, attrs);
    }
    if (result != MEMTX_OK) {
        s->running = false;
        nikon_transfer_error(s, 0xc18, 4, true, 1, "transfer-dma-failed");
        return;
    }
    s->source += length;
    s->destination += length;
    s->remaining -= length;
    if (s->remaining) {
        nikon_transfer_schedule(s);
    } else {
        s->running = false;
        s->completed = true;
        nikon_transfer_irq(s);
    }
}

static int nikon_transfer_register(hwaddr offset)
{
    unsigned i;

    for (i = 0; i < TRANSFER_REGISTER_COUNT; i++) {
        if (register_offsets[i] == offset) {
            return i;
        }
    }
    return -1;
}

static MemTxResult nikon_transfer_read(void *opaque, hwaddr offset,
                                      uint64_t *value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonTransferState *s = opaque;
    int index = nikon_transfer_register(offset);

    if (size != 4 || index < 0) {
        return nikon_transfer_error(s, offset, size, false, 0,
                                    "transfer-read-not-supported");
    }
    *value = s->reg[index];
    return MEMTX_OK;
}

static MemTxResult nikon_transfer_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    NikonTransferState *s = opaque;
    int index = nikon_transfer_register(offset);
    static const unsigned byte_factors[] = { 4, 3, 2, 2, 2, 1 };
    uint32_t required = (1U << TRANSFER_SOURCE) |
                        (1U << TRANSFER_DESTINATION) |
                        (1U << TRANSFER_CONTROL);
    uint64_t extent;
    unsigned selector;

    if (size != 4 || index < 0) {
        goto unsupported;
    }
    if (index != TRANSFER_START) {
        if (s->running) {
            goto unsupported;
        }
        s->reg[index] = value;
        s->configured |= 1U << index;
        return MEMTX_OK;
    }
    if (!value) {
        timer_del(s->timer);
        s->running = s->completed = false;
        s->reg[index] = 0;
        nikon_transfer_irq(s);
        return MEMTX_OK;
    }
    if (value != 1 || s->running || s->completed ||
        (s->configured & required) != required) {
        goto unsupported;
    }
    if (s->reg[TRANSFER_CONTROL] == 8) {
        required = (1U << TRANSFER_EXTENT) |
                   (1U << TRANSFER_EXTENSION) |
                   (1U << TRANSFER_PADDING);
        if ((s->configured & required) != required ||
            s->reg[TRANSFER_EXTENSION] || s->reg[TRANSFER_PADDING]) {
            goto unsupported;
        }
        /* The original producer sends a second command for secondary data. */
        extent = s->reg[TRANSFER_EXTENT];
    } else {
        required = (1U << TRANSFER_SOURCE_GEOMETRY) |
                   (1U << TRANSFER_DESTINATION_GEOMETRY);
        selector = (s->reg[TRANSFER_CONTROL] >> 8) & 7;
        if ((s->configured & required) != required ||
            s->reg[TRANSFER_CONTROL] & ~0x00007700U ||
            selector >= ARRAY_SIZE(byte_factors) ||
            selector != ((s->reg[TRANSFER_CONTROL] >> 12) & 7) ||
            s->reg[TRANSFER_SOURCE_GEOMETRY] !=
            s->reg[TRANSFER_DESTINATION_GEOMETRY] ||
            s->reg[TRANSFER_SECONDARY_SOURCE] ||
            s->reg[TRANSFER_SECONDARY_DESTINATION]) {
            goto unsupported;
        }
        extent = (uint64_t)(s->reg[TRANSFER_SOURCE_GEOMETRY] & 0xffff) *
                 (s->reg[TRANSFER_SOURCE_GEOMETRY] >> 16) *
                 byte_factors[selector];
    }
    if (!extent || extent > UINT32_MAX ||
        s->reg[TRANSFER_SOURCE] > UINT32_MAX - extent ||
        s->reg[TRANSFER_DESTINATION] > UINT32_MAX - extent) {
        goto unsupported;
    }
    s->source = s->reg[TRANSFER_SOURCE];
    s->destination = s->reg[TRANSFER_DESTINATION];
    s->remaining = extent;
    if (!nikon_transfer_range(s->source, s->remaining, false) ||
        !nikon_transfer_range(s->destination, s->remaining, true)) {
        return nikon_transfer_error(s, offset, size, true, value,
                                    "transfer-memory-range-not-backed");
    }
    s->reg[index] = value;
    s->running = true;
    nikon_transfer_schedule(s);
    return MEMTX_OK;
unsupported:
    return nikon_transfer_error(s, offset, size, true, value,
                                "transfer-packet-not-supported");
}

static const MemoryRegionOps nikon_transfer_ops = {
    .read_with_attrs = nikon_transfer_read,
    .write_with_attrs = nikon_transfer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static void nikon_transfer_reset(DeviceState *dev)
{
    NikonTransferState *s = NIKON_TRANSFER(dev);

    timer_del(s->timer);
    memset(s->reg, 0, sizeof(s->reg));
    s->configured = 0;
    s->source = s->destination = s->remaining = 0;
    s->running = s->completed = s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    nikon_transfer_irq(s);
}

static void nikon_transfer_init(Object *obj)
{
    NikonTransferState *s = NIKON_TRANSFER(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "transfer-clock");
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_transfer_step, s);
    memory_region_init_io(&s->regs, obj, &nikon_transfer_ops, s,
                          "nikon.transfer", 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (i = 0; i < 3; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
}

static void nikon_transfer_finalize(Object *obj)
{
    timer_free(NIKON_TRANSFER(obj)->timer);
}

static const VMStateDescription nikon_transfer_vmstate = {
    .name = "nikon-transfer", .unmigratable = true,
};

static void nikon_transfer_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_transfer_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_transfer_reset);
}

static const TypeInfo nikon_transfer_type = {
    .name = TYPE_NIKON_TRANSFER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonTransferState),
    .instance_init = nikon_transfer_init,
    .instance_finalize = nikon_transfer_finalize,
    .class_init = nikon_transfer_class_init,
};

static void nikon_transfer_register_types(void)
{
    type_register_static(&nikon_transfer_type);
}
type_init(nikon_transfer_register_types)
