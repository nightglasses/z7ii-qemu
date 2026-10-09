/*
 * Finite MAS 2013 control/bridge protocol, not an identified Nikon IP.
 * Original bounded-source contract23d2601: init/request/wait/ISR and the
 * five translation arrays. Reset, 10us timing and internal IRQ causes below
 * are explicit virtual conventions. Peer events require connected input edges;
 * Peer windows forward guest-published mappings into a borrowed peer address
 * space. No peer data, reply, signature or readiness is synthesized.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "qemu/bswap.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonBridgeState, NIKON_BRIDGE)

typedef struct NikonBridgeBank {
    NikonBridgeState *bridge;
    MemoryRegion aperture;
    AddressSpace *peer;
    QEMUTimer *timer;
    uint32_t request;
    uint32_t pending;
    uint32_t lane_status[8];
    uint32_t context[8][15];
    uint16_t context_written[8];
    uint32_t selector;
    uint32_t window[4][3];
    uint32_t configuration[15];
    uint32_t mapping[40];
} NikonBridgeBank;

struct NikonBridgeState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Clock *clock;
    qemu_irq irq;
    qemu_irq peer_start;
    qemu_irq peer_configuration;
    QEMUTimer *control_timer;
    QEMUTimer *configuration_timer;
    NikonBridgeBank bank[2];
    uint32_t reset;
    uint32_t routing[2];
    uint32_t command;
    uint32_t ready;
    uint32_t irq_status;
    uint32_t irq_enable;
    uint32_t peer_ack;
    bool peer_event_level[3];
    bool peer_control_level[2];
    bool peer_ready;
    bool configuration_pending;
    bool reported_error;
};

/* Complete recovered word ports; sampled initializer values are not limits. */
static const unsigned context_ports[] = {
    0x84, 0x88, 0x8c, 0x90, 0xa0, 0xa4, 0xa8, 0xb0,
    0xb4, 0xbc, 0xcc, 0xdc, 0xe0, 0xe4, 0xac,
};
static const unsigned bank_ports[] = {
    0x4104, 0x412c, 0x4130, 0x4134, 0x4140, 0x4400, 0x4504,
    0x450c, 0x4520, 0x45fc, 0x5004, 0x504c, 0x4120, 0x4124, 0x4910,
};

static void nikon_bridge_irq(NikonBridgeState *s)
{
    qemu_set_irq(s->irq, !!(s->irq_status & s->irq_enable));
}

static void nikon_bridge_peer_event(void *opaque, int n, int level)
{
    static const unsigned bits[] = { 0, 2, 5 };
    NikonBridgeState *s = opaque;
    bool high = !!level;

    /* Disconnected inputs are low. Reset release never invents an edge. */
    if (high && !s->peer_event_level[n] && !s->reset) {
        s->irq_status |= 1U << bits[n];
        nikon_bridge_irq(s);
    }
    s->peer_event_level[n] = high;
}

static void nikon_bridge_peer_ready(void *opaque, int n, int level)
{
    NikonBridgeState *s = opaque;

    /* The ISR separately consumes bank1 C908.bit2; this is not bank0 done. */
    s->peer_ready = !!level;
}

static void nikon_bridge_peer_control(void *opaque, int n, int level)
{
    static const unsigned bits[] = { 0, 6 };
    NikonBridgeState *s = opaque;
    bool high = !!level;

    /* Connected control requests latch raw state, never guest event flags. */
    if (high && !s->peer_control_level[n] && !s->reset) {
        s->irq_status |= 1U << bits[n];
        nikon_bridge_irq(s);
    }
    s->peer_control_level[n] = high;
}

static int64_t nikon_bridge_deadline(NikonBridgeState *s)
{
    /* Independent 48 MHz, 480 ticks = 10 us is a virtual apply latency. */
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
           clock_ticks_to_ns(s->clock, 480);
}

static void nikon_bridge_lanes_done(void *opaque)
{
    NikonBridgeBank *b = opaque;
    unsigned i;

    for (i = 0; i < 8; i++) {
        if (b->pending & (1U << i)) {
            b->lane_status[(i & 3) * 2 + (i >= 4)] = 3;
        }
    }
    b->pending = 0;
    /* Lane status completion does not fabricate the peer's event1/IRQbit0. */
}

static void nikon_bridge_control_done(void *opaque)
{
    NikonBridgeState *s = opaque;

    if (s->command == 6) {
        s->ready = 1;
    }
    s->irq_status |= 2;
    nikon_bridge_irq(s);
    if (s->command == 6) {
        qemu_irq_pulse(s->peer_start);
    }
}

static void nikon_bridge_configuration_done(void *opaque)
{
    NikonBridgeState *s = opaque;

    /* Command2 precedes array publication in the original caller. */
    s->configuration_pending = false;
    s->irq_status |= 0x40;
    nikon_bridge_irq(s);
    qemu_irq_pulse(s->peer_configuration);
}

static void nikon_bridge_cancel(NikonBridgeState *s)
{
    unsigned bank;

    timer_del(s->control_timer);
    timer_del(s->configuration_timer);
    s->command = s->ready = s->irq_status = 0;
    s->peer_ack = 0;
    s->configuration_pending = false;
    for (bank = 0; bank < 2; bank++) {
        NikonBridgeBank *b = &s->bank[bank];

        timer_del(b->timer);
        b->pending = b->request = b->selector = 0;
        memset(b->lane_status, 0, sizeof(b->lane_status));
        memset(b->context, 0, sizeof(b->context));
        memset(b->context_written, 0, sizeof(b->context_written));
        memset(b->window, 0, sizeof(b->window));
        memset(b->configuration, 0, sizeof(b->configuration));
        memset(b->mapping, 0, sizeof(b->mapping));
    }
    nikon_bridge_irq(s);
}

static int nikon_bridge_field(unsigned offset)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(context_ports); i++) {
        if (context_ports[i] == offset) {
            return i;
        }
    }
    return -1;
}

static MemTxResult nikon_bridge_error(NikonBridgeState *s, hwaddr offset,
                                    unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size, write,
                      value, "bridge-access-or-protocol-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_bridge_access(NikonBridgeState *s, hwaddr offset,
                                     uint64_t *value, unsigned size, bool write)
{
    NikonBridgeBank *b = &s->bank[offset >= 0x8000];
    unsigned local = offset & 0x7fff;
    unsigned context, reg, i;
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
                nikon_bridge_cancel(s);
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
    case 0xc908:
        if (write) {
            goto error;
        }
        *value = s->peer_ready ? 4 : 0;
        return MEMTX_OK;
    case 0x829c:
        /* Original IRQbit2 path writes bank base 029c with bankmask2. */
        if (!write || *value != 1 || s->reset) {
            goto error;
        }
        s->peer_ack = 1;
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
            timer_mod(s->control_timer, nikon_bridge_deadline(s));
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
            nikon_bridge_irq(s);
        } else {
            *value = s->irq_enable;
        }
        return MEMTX_OK;
    case 0xd028:
        if (!write || (*value & ~0xffffU)) {
            goto error;
        }
        s->irq_status &= ~*value;
        nikon_bridge_irq(s);
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
            if (!b->selector) {
                goto error;
            }
            context = ctz32(b->selector >> 16);
            if (write) {
                if (s->reset || b->pending) {
                    goto error;
                }
                b->window[context][i] = *value;
            } else {
                *value = b->window[context][i];
            }
            return MEMTX_OK;
        }
        field = nikon_bridge_field(reg);
        if (field < 0 || ((local & 0x200) && reg != 0x84 && reg != 0x88 &&
                         reg != 0x8c && reg != 0x90)) {
            goto error;
        }
        if (write) {
            if (s->reset || b->pending) {
                goto error;
            }
            b->context[context][field] = *value;
            b->context_written[context] |= 1U << field;
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

                    if ((b->context_written[c] & 6) != 6) {
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
            timer_mod(b->timer, nikon_bridge_deadline(s));
        } else {
            *value = b->request;
        }
        return MEMTX_OK;
    }
    if (local >= 0x50b0 && local < 0x5150) {
        i = (local - 0x50b0) / 4;
        if (write) {
            if (s->reset || s->configuration_pending) {
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
        timer_mod(s->configuration_timer, nikon_bridge_deadline(s));
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(bank_ports); i++) {
        if (local == bank_ports[i]) {
            if (write) {
                if (s->reset || b->pending) {
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
    return nikon_bridge_error(s, offset, size, write, write ? *value : 0);
}

static MemTxResult nikon_bridge_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    *value = 0;
    return nikon_bridge_access(opaque, offset, value, size, false);
}

static MemTxResult nikon_bridge_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    return nikon_bridge_access(opaque, offset, &value, size, true);
}

static const MemoryRegionOps nikon_bridge_ops = {
    .read_with_attrs = nikon_bridge_read,
    .write_with_attrs = nikon_bridge_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

/*
 * 1038816a publishes remote page, inverted length mask and local page through
 * DDC76's five arrays. This functional interpretation supports its aligned,
 * power-of-two chunks. Unused zero rows and unknown nonzero attribute arrays
 * do not create windows. Array publication itself is not an IRQ/commit.
 */
static bool nikon_bridge_translate(NikonBridgeBank *b, hwaddr address,
                                   unsigned size, hwaddr *remote)
{
    unsigned i;
    bool matched = false;

    if (!size || address > UINT32_MAX - (size - 1)) {
        return false;
    }
    for (i = 0; i < 8; i++) {
        uint32_t source = b->mapping[i];
        uint32_t mask = b->mapping[8 + i];
        uint32_t local = b->mapping[16 + i];
        uint32_t pages = ((~mask) & 0xfffffU) + 1;
        uint64_t base = (uint64_t)local << 12;
        uint64_t length = (uint64_t)pages << 12;

        if (!mask || ((source | mask | local) & ~0xfffffU) ||
            !is_power_of_2(pages) || ((source | local) & (pages - 1)) ||
            b->mapping[24 + i] || b->mapping[32 + i] ||
            address < base || address + size > base + length) {
            continue;
        }
        /* Ambiguous overlapping rows must not silently pick the first peer. */
        if (matched) {
            return false;
        }
        *remote = ((hwaddr)source << 12) + address - base;
        if (*remote > UINT32_MAX - (size - 1)) {
            return false;
        }
        matched = true;
    }
    return matched;
}

static MemTxResult nikon_bridge_window_access(NikonBridgeBank *b,
                                             hwaddr address, uint64_t *value,
                                             unsigned size, bool write,
                                             MemTxAttrs attrs)
{
    NikonBridgeState *s = b->bridge;
    hwaddr remote;
    uint8_t bytes[8] = { 0 };
    MemTxResult result;

    if (!b->peer || s->reset || !is_power_of_2(size) ||
        size > sizeof(bytes) ||
        !nikon_bridge_translate(b, address, size, &remote)) {
        nikon_access_error(&s->reported_error, address, size, write,
                           write ? *value : 0,
                           "bridge-window-or-peer-not-supported");
        return MEMTX_ERROR;
    }
    if (write) {
        stq_le_p(bytes, *value);
    }
    result = address_space_rw(b->peer, remote, attrs, bytes, size, write);
    if (!write && result == MEMTX_OK) {
        *value = ldq_le_p(bytes);
    }
    return result;
}

static MemTxResult nikon_bridge_window_read(void *opaque, hwaddr offset,
                                           uint64_t *value, unsigned size,
                                           MemTxAttrs attrs)
{
    *value = 0;
    return nikon_bridge_window_access(opaque, offset, value, size, false,
                                      attrs);
}

static MemTxResult nikon_bridge_window_write(void *opaque, hwaddr offset,
                                            uint64_t value, unsigned size,
                                            MemTxAttrs attrs)
{
    return nikon_bridge_window_access(opaque, offset, &value, size, true,
                                      attrs);
}

static const MemoryRegionOps nikon_bridge_window_ops = {
    .read_with_attrs = nikon_bridge_window_read,
    .write_with_attrs = nikon_bridge_window_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

void nikon_bridge_attach_peer(DeviceState *dev, unsigned bank,
                              AddressSpace *peer)
{
    NikonBridgeState *s = NIKON_BRIDGE(dev);

    assert(bank < ARRAY_SIZE(s->bank));
    s->bank[bank].peer = peer;
}

static void nikon_bridge_reset(DeviceState *dev)
{
    NikonBridgeState *s = NIKON_BRIDGE(dev);

    s->reset = s->routing[0] = s->routing[1] = s->irq_enable = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    nikon_bridge_cancel(s);
}

static void nikon_bridge_init(Object *obj)
{
    NikonBridgeState *s = NIKON_BRIDGE(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-bridge-clock");
    s->control_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    nikon_bridge_control_done, s);
    s->configuration_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                         nikon_bridge_configuration_done, s);
    for (i = 0; i < 2; i++) {
        g_autofree char *name = g_strdup_printf("nikon.bridge-window%u", i);

        s->bank[i].bridge = s;
        s->bank[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       nikon_bridge_lanes_done, &s->bank[i]);
        memory_region_init_io(&s->bank[i].aperture, obj,
                              &nikon_bridge_window_ops, &s->bank[i], name,
                              UINT64_C(0x100000000));
    }
    memory_region_init_io(&s->regs, obj, &nikon_bridge_ops, s,
                          "nikon.bridge", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    /* The machine maps only its source-qualified aliases of these windows. */
    for (i = 0; i < 2; i++) {
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->bank[i].aperture);
    }
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_bridge_peer_event,
                           "peer-event", 3);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_bridge_peer_ready,
                           "peer-ready", 1);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_bridge_peer_control,
                           "peer-control", 2);
    qdev_init_gpio_out_named(DEVICE(obj), &s->peer_start, "peer-start", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->peer_configuration,
                            "peer-configuration", 1);
}

static void nikon_bridge_finalize(Object *obj)
{
    NikonBridgeState *s = NIKON_BRIDGE(obj);
    unsigned i;

    timer_free(s->control_timer);
    timer_free(s->configuration_timer);
    for (i = 0; i < 2; i++) {
        timer_free(s->bank[i].timer);
    }
}

static const VMStateDescription nikon_bridge_vmstate = {
    .name = "nikon-bridge", .unmigratable = true,
};

static void nikon_bridge_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_bridge_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_bridge_reset);
}

static const TypeInfo nikon_bridge_type = {
    .name = TYPE_NIKON_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonBridgeState),
    .instance_init = nikon_bridge_init,
    .instance_finalize = nikon_bridge_finalize,
    .class_init = nikon_bridge_class_init,
};

static void nikon_bridge_register_types(void)
{
    type_register_static(&nikon_bridge_type);
}
type_init(nikon_bridge_register_types)
