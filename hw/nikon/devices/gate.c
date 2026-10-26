/*
 * Finite original 2010 configuration, masks and external pending inputs.
 * Cold zero, retained configuration readback, rising input latches, mask1
 * enable, external gate, deferred stop ACK, submit-pending state and W1C are
 * virtual laws, not pipeline semantics. Type0 parameter slots retain busy
 * ownership after submission; no unbound processor is treated as complete.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/timer.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/gatepacket.h"

#define CONTROL_SIZE 0x4080
#define INPUT_SLOTS 16
#define INPUT_WORDS 13
#define TABLE_SIZE 0x6000
#define WORKSPACE_SIZE 0x1000

/* MAS10789fe0..1078a28f resource rows join1078a2a0's backend pointers. */
static const struct {
    uint16_t offset;
    uint8_t kind;
} command_ports[] = {
    { 0x800, 4 }, { 0x820, 4 }, { 0x840, 4 }, { 0x880, 7 },
    { 0x940, 10 }, { 0x960, 10 }, { 0x980, 10 }, { 0x9a0, 10 },
    { 0x9c0, 10 }, { 0x9e0, 10 }, { 0xa00, 17 }, { 0xa20, 17 },
    { 0xa40, 12 }, { 0xb60, 17 }, { 0xb80, 17 }, { 0xba0, 10 },
    { 0xbc0, 10 }, { 0xd00, 15 }, { 0xd20, 15 }, { 0xd40, 11 },
    { 0xd60, 11 }, { 0xd80, 11 }, { 0xda0, 11 }, { 0xdc0, 11 },
    { 0xde0, 11 }, { 0xe00, 11 }, { 0xe20, 11 },
    { 0xe60, 5 }, { 0xe80, 6 },
    { 0x8a0, 8 }, { 0x8c0, 8 }, { 0x8e0, 8 }, { 0xc80, 8 },
    { 0x900, 9 }, { 0xac0, 9 }, { 0xcc0, 9 }, { 0xce0, 9 },
    { 0xa80, 13 }, { 0xaa0, 13 }, { 0xa60, 14 },
    { 0xe40, 16 }, { 0x920, 18 },
};

G_STATIC_ASSERT(ARRAY_SIZE(command_ports) <= 64);

typedef struct GateCommand {
    NikonGatePacket packet;
    uint32_t address, auxiliary[2];
    bool reset, armed;
} GateCommand;

OBJECT_DECLARE_SIMPLE_TYPE(NikonEventGateState, NIKON_EVENT_GATE)

struct NikonEventGateState {
    SysBusDevice parent_obj;
    MemoryRegion regs, tables, workspace;
    qemu_irq irq;
    uint32_t config[CONTROL_SIZE / 4];
    uint32_t mask[3], pending[3], inputs[3];
    uint32_t slot[INPUT_SLOTS][INPUT_WORDS];
    uint16_t written[INPUT_SLOTS], busy;
    GateCommand command[ARRAY_SIZE(command_ports)];
    uint16_t table[TABLE_SIZE / 4];
    uint32_t work[WORKSPACE_SIZE / 4], workspace_written;
    bool work_written[WORKSPACE_SIZE / 4];
    uint64_t configured;
    uint64_t loaded, loaded_words;
    QEMUTimer *stop_timer;
    bool stopping, stop_ack, submit_pending;
    bool reported_error;
};

static bool nikon_event_gate_slot(hwaddr offset, unsigned *slot, unsigned *word)
{
    if (offset < 0x400 || offset >= 0x800 || (offset & 0x3f) >= 0x34) {
        return false;
    }
    *slot = (offset - 0x400) / 0x40;
    *word = (offset & 0x3f) / 4;
    return true;
}

/* MAS103bda1e emits the complete packet, including overlapping byte fields. */
static const uint32_t slot_masks[INPUT_WORDS] = {
    0xff771f71, 0x0001ff3f, UINT32_MAX, UINT32_MAX,
    0x0001ffff, 0x0001ffff, 0x0001ffff, 0x0001ffff,
    0x3fff7fff, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
};

/* Finite setup shapes, not a physical writable-bit specification. */
static bool nikon_event_gate_config(hwaddr offset, uint32_t *mask)
{
    hwaddr local;

    *mask = UINT32_MAX;
    if (offset >= 0x4000 && offset < 0x4080 && (offset & 0xf) <= 4) {
        /* MAS103bdc12: eight paired type1 endpoints, two packed words each. */
        *mask = offset & 4 ? 0x3fff7fff : 0x773;
        return true;
    }
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
            /* SOURCE11 94640 clears bit0 then ORs an unclamped word. */
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

static int nikon_event_gate_command(hwaddr offset)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(command_ports); i++) {
        if (offset >= command_ports[i].offset &&
            offset < command_ports[i].offset + 0x20) {
            return i;
        }
    }
    return -1;
}

static bool nikon_event_gate_ram(hwaddr address, size_t length)
{
    if (!address || address > UINT32_MAX - length) {
        return false;
    }
    while (length) {
        MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                        address, length);
        uint64_t span;
        bool readable;

        if (!section.mr) {
            return false;
        }
        span = int128_get64(section.size);
        readable = span && span <= length &&
                   section.offset_within_address_space == address &&
                   memory_region_is_ram(section.mr) &&
                   memory_access_is_direct(section.mr, false,
                                            MEMTXATTRS_UNSPECIFIED);
        memory_region_unref(section.mr);
        if (!readable) {
            return false;
        }
        address += span;
        length -= span;
    }
    return true;
}

static bool nikon_event_gate_load(NikonEventGateState *s, unsigned index,
                                uint32_t address)
{
    GateCommand *command = &s->command[index];
    unsigned kind = command_ports[index].kind;
    size_t length = nikon_gate_packet_size(kind);
    uint8_t data[NIKON_GATE_PACKET_MAX];
    NikonGatePacket decoded;
    MemTxAttrs attrs = { .memory = true };

    if (!command->armed || command->reset || (address & 3) ||
        !nikon_event_gate_ram(address, length) ||
        dma_memory_read(&address_space_memory, address, data, length, attrs) !=
        MEMTX_OK || !nikon_gate_packet_decode(kind, data, length, &decoded)) {
        return false;
    }
    command->packet = decoded;
    command->address = address;
    command->armed = false;
    s->configured |= UINT64_C(1) << index;
    s->loaded++;
    s->loaded_words += length / 4;
    return true;
}

static MemTxResult nikon_event_gate_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonEventGateState *s = opaque;
    uint32_t mask;
    unsigned slot, word;
    int index;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_event_gate_error(s, offset, size, false, 0);
    }
    index = nikon_event_gate_command(offset);
    if (index >= 0) {
        GateCommand *command = &s->command[index];
        hwaddr local = offset - command_ports[index].offset;

        switch (local) {
        case 0:
            /*
             * 103bdbbe tests (status & 3) != 3. No completed result exists;
             * zero is an explicit incomplete-state convention, not idle.
             */
            *value = 0;
            return MEMTX_OK;
        case 0x10:
            *value = command->address;
            return MEMTX_OK;
        case 0x14:
            *value = command->reset;
            return MEMTX_OK;
        case 8: case 0xc:
            if (command_ports[index].kind == 10) {
                *value = command->auxiliary[(local - 8) / 4];
                return MEMTX_OK;
            }
        }
        return nikon_event_gate_error(s, offset, size, false, 0);
    }
    if (nikon_event_gate_slot(offset, &slot, &word)) {
        *value = s->slot[slot][word];
        return MEMTX_OK;
    }
    if (offset >= 0x2040 && offset < 0x2080) {
        *value = !!(s->busy & (1U << ((offset - 0x2040) / 4)));
        return MEMTX_OK;
    }
    if (offset == 0x148) {
        /*
         * 103bdd8c clears all four work banks while bit8 is absent. The
         * finite provision state records real CPU writes, not a data result.
         */
        *value = s->workspace_written == ARRAY_SIZE(s->work) ? 0x100 : 0;
        return MEMTX_OK;
    }
    if (nikon_event_gate_config(offset, &mask)) {
        /* Retained setup values, never unbound data/monitor word defaults. */
        *value = s->config[offset / 4];
        return MEMTX_OK;
    }
    if (offset == 0x2800) {
        /* Source 94996 has no completion/readback producer; expose only the
         * recorded outstanding request for focused contract tests. */
        *value = s->submit_pending;
    } else if (offset >= 0xc && offset <= 0x14) {
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
    unsigned slot, word;
    int index;

    if (size != 4 || (offset & 3)) {
        return nikon_event_gate_error(s, offset, size, true, value);
    }
    index = nikon_event_gate_command(offset);
    if (index >= 0) {
        GateCommand *command = &s->command[index];
        hwaddr local = offset - command_ports[index].offset;

        if (local == 0x14 && value <= 1 && (value || command->reset)) {
            if (value) {
                /*103bdc92 programs auxiliary words before this pulse. */
                memset(&command->packet, 0, sizeof(command->packet));
                command->address = 0;
                command->armed = false;
                command->reset = true;
                s->configured &= ~(UINT64_C(1) << index);
            } else {
                command->reset = false;
                command->armed = true;
            }
            return MEMTX_OK;
        }
        if (local == 0x10 && nikon_event_gate_load(s, index, value)) {
            return MEMTX_OK;
        }
        if ((local == 8 || local == 0xc) &&
            command_ports[index].kind == 10) {
            command->auxiliary[(local - 8) / 4] = value;
            return MEMTX_OK;
        }
        /*
         * Actual processor activation is unsupported. Configuration import
         * alone establishes neither an input dependency nor a completion.
         */
        return nikon_event_gate_error(s, offset, size, true, value);
    }
    if (nikon_event_gate_slot(offset, &slot, &word)) {
        if ((s->busy & (1U << slot)) || (value & ~slot_masks[word])) {
            return nikon_event_gate_error(s, offset, size, true, value);
        }
        s->slot[slot][word] = value;
        s->written[slot] |= 1U << word;
        return MEMTX_OK;
    }
    if (offset == 0x2c) {
        if (value >= INPUT_SLOTS || (s->busy & (1U << value)) ||
            s->written[value] != (1U << INPUT_WORDS) - 1 ||
            !(s->slot[value][0] & 1)) {
            return nikon_event_gate_error(s, offset, size, true, value);
        }
        /* Parameter ownership only. Processing needs its real graph consumer;
         * elapsed time, W1C and the existing stop ACK cannot retire this work. */
        s->busy |= 1U << value;
        s->written[value] = 0;
        return MEMTX_OK;
    }
    if (offset == 0x2c0 && value == 1) {
        /* 103bdd8c requests new provisioning before CPU workspace writes. */
        memset(s->work_written, 0, sizeof(s->work_written));
        s->workspace_written = 0;
        return MEMTX_OK;
    }
    if (offset == 0x2800) {
        /* 94996 submits one; no ready bit, completion, IRQ, guest write or
         * pixel producer is established at this boundary. */
        if (value != 1) {
            return nikon_event_gate_error(s, offset, size, true, value);
        }
        s->submit_pending = true;
        return MEMTX_OK;
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
        /* Includes 2680 requests and all unbound data/monitor words. */
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

static MemTxResult nikon_event_gate_table_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonEventGateState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        nikon_access_error(&s->reported_error, s->tables.addr + offset, size,
                           false, 0, "event-gate-table-access-not-supported");
        return MEMTX_ERROR;
    }
    *value = s->table[offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_event_gate_table_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size, MemTxAttrs attrs)
{
    NikonEventGateState *s = opaque;

    if (size != 4 || (offset & 3) || value > UINT16_MAX) {
        nikon_access_error(&s->reported_error, s->tables.addr + offset, size,
                           true, value,
                           "event-gate-table-access-or-value-not-supported");
        return MEMTX_ERROR;
    }
    s->table[offset / 4] = value;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_event_gate_table_ops = {
    .read_with_attrs = nikon_event_gate_table_read,
    .write_with_attrs = nikon_event_gate_table_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static MemTxResult nikon_event_gate_workspace_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonEventGateState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        nikon_access_error(&s->reported_error, s->workspace.addr + offset, size,
                           false, 0,
                           "event-gate-workspace-access-not-supported");
        return MEMTX_ERROR;
    }
    *value = s->work[offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_event_gate_workspace_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size, MemTxAttrs attrs)
{
    NikonEventGateState *s = opaque;
    unsigned word = offset / 4;

    if (size != 4 || (offset & 3)) {
        nikon_access_error(&s->reported_error, s->workspace.addr + offset, size,
                           true, value,
                           "event-gate-workspace-access-not-supported");
        return MEMTX_ERROR;
    }
    s->work[word] = value;
    if (!s->work_written[word]) {
        s->work_written[word] = true;
        s->workspace_written++;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_event_gate_workspace_ops = {
    .read_with_attrs = nikon_event_gate_workspace_read,
    .write_with_attrs = nikon_event_gate_workspace_write,
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
    s->submit_pending = false;
    s->busy = 0;
    memset(s->slot, 0, sizeof(s->slot));
    memset(s->written, 0, sizeof(s->written));
    memset(s->command, 0, sizeof(s->command));
    memset(s->table, 0, sizeof(s->table));
    memset(s->work, 0, sizeof(s->work));
    memset(s->work_written, 0, sizeof(s->work_written));
    s->workspace_written = 0;
    s->configured = 0;
    s->loaded = 0;
    s->loaded_words = 0;
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
    memory_region_init_io(&s->tables, obj, &nikon_event_gate_table_ops, s,
                          "nikon.event-gate-tables", TABLE_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->tables);
    memory_region_init_io(&s->workspace, obj, &nikon_event_gate_workspace_ops,
                          s, "nikon.event-gate-workspace", WORKSPACE_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->workspace);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_event_gate_input, "pending-bit", 96);
    object_property_add_uint64_ptr(obj, "configured-mask", &s->configured,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "loaded-packets", &s->loaded,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "loaded-words", &s->loaded_words,
                                   OBJ_PROP_FLAG_READ);
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
