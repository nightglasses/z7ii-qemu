/*
 * Two-channel Audio I/F functional profile, original MAS register protocol.
 * FIFO payloads are opaque words: no physical PCM packing is inferred.
 * Reset, independent nominal clock and disconnected input are board assumptions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/fifo32.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IIAudioState, Z7II_AUDIO)

typedef struct Z7IIAudioChannel {
    Z7IIAudioState *owner;
    uint32_t control, mode, status, divider, port;
    Fifo32 output[2];
    QEMUTimer *timer;
    qemu_irq irq;
    unsigned index;
} Z7IIAudioChannel;

struct Z7IIAudioState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Clock *clock;
    uint32_t reset;
    Z7IIAudioChannel channel[2];
    qemu_irq request[3];
    bool reported_error;
};

static unsigned z7ii_audio_capacity(Z7IIAudioChannel *c)
{
    return (c->mode & (1U << 25)) ? 16 : 1;
}

static unsigned z7ii_audio_used(Z7IIAudioChannel *c)
{
    unsigned used = fifo32_num_used(&c->output[0]);

    if (c->control & 2) {
        used = MAX(used, fifo32_num_used(&c->output[1]));
    }
    return used;
}

static unsigned z7ii_audio_threshold(Z7IIAudioChannel *c)
{
    static const unsigned threshold[] = { 1, 12, 8, 4 };

    return MIN(z7ii_audio_capacity(c), threshold[c->status >> 30]);
}

static uint32_t z7ii_audio_control(Z7IIAudioChannel *c)
{
    /* No external input source. Output empty derives from real queue contents. */
    return c->control | (z7ii_audio_used(c) ? 0 : 1U << 9);
}

static uint32_t z7ii_audio_status(Z7IIAudioChannel *c)
{
    bool available = z7ii_audio_capacity(c) - z7ii_audio_used(c) >=
                     z7ii_audio_threshold(c);

    return c->status | (available ? 1U << 25 : 0);
}

static void z7ii_audio_update(Z7IIAudioChannel *c)
{
    uint32_t control = z7ii_audio_control(c);
    uint32_t status = z7ii_audio_status(c);
    bool enabled = (c->control & (1U << 13)) &&
                   !(c->owner->reset & (1U << c->index));
    bool demand = enabled && !(c->control & 2) &&
                  z7ii_audio_used(c) < z7ii_audio_capacity(c) &&
                  ((c->control & (1U << 15)) ||
                   ((c->status & (1U << 29)) && (status & (1U << 25))));
    bool irq = enabled && (((control & (1U << 9)) &&
                           (c->control & (1U << 11))) ||
                          ((status & (1U << 25)) &&
                           (c->status & (1U << 27))) ||
                          ((status & (1U << 17)) &&
                           (c->status & (1U << 19))));

    qemu_set_irq(c->irq, irq);
    qemu_set_irq(c->owner->request[c->index], demand);
    /* Source selector 5 remains low: no invented input samples or clocks. */
    qemu_set_irq(c->owner->request[2], 0);
}

static int64_t z7ii_audio_period(Z7IIAudioChannel *c)
{
    uint64_t ticks = (((c->divider >> 24) & 31) + 1) *
                     (4 * (((c->divider >> 18) & 7) + 1)) *
                     (16 * (((c->divider >> 16) & 3) + 1));

    return MAX(1, clock_ticks_to_ns(c->owner->clock, ticks));
}

static void z7ii_audio_schedule(Z7IIAudioChannel *c)
{
    if ((c->control & (1U << 13)) && (c->divider & (1U << 30)) &&
        !(c->owner->reset & (1U << c->index)) && !timer_pending(c->timer)) {
        timer_mod(c->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                           z7ii_audio_period(c));
    }
}

static void z7ii_audio_sink(void *opaque)
{
    Z7IIAudioChannel *c = opaque;
    bool separate = c->control & 2;

    if (!(c->control & (1U << 13)) || !(c->divider & (1U << 30)) ||
        (c->owner->reset & (1U << c->index))) {
        return;
    }
    if (!fifo32_is_empty(&c->output[0]) &&
        (!separate || !fifo32_is_empty(&c->output[1]))) {
        /* Virtual endpoint consumes real words, without guessing PCM layout. */
        fifo32_pop(&c->output[0]);
        if (separate) {
            fifo32_pop(&c->output[1]);
        }
    } else {
        c->status |= 1U << 17;
    }
    z7ii_audio_update(c);
    z7ii_audio_schedule(c);
}

static void z7ii_audio_channel_reset(Z7IIAudioChannel *c)
{
    timer_del(c->timer);
    c->control = c->mode = c->status = c->divider = c->port = 0;
    fifo32_reset(&c->output[0]);
    fifo32_reset(&c->output[1]);
    z7ii_audio_update(c);
}

static MemTxResult z7ii_audio_error(Z7IIAudioState *s, hwaddr offset,
                                   unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, 0x2c080000 + offset, size, write,
                      value, "audio-access-or-profile-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_audio_read(void *opaque, hwaddr offset, uint64_t *value,
                                  unsigned size, MemTxAttrs attrs)
{
    Z7IIAudioState *s = opaque;
    Z7IIAudioChannel *c;
    unsigned lane, used, i;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_audio_error(s, offset, size, false, 0);
    }
    if (!offset) {
        *value = s->reset;
        return MEMTX_OK;
    }
    if (offset < 0x100 || offset >= 0x250) {
        return z7ii_audio_error(s, offset, size, false, 0);
    }
    c = &s->channel[(offset - 0x100) >> 8];
    switch (offset & 0xff) {
    case 8:
        *value = z7ii_audio_control(c);
        break;
    case 12:
        *value = c->mode;
        break;
    case 16:
        *value = z7ii_audio_status(c);
        break;
    case 20:
        *value = c->divider;
        break;
    case 24:
        *value = c->port;
        break;
    case 28:
        /* Disconnected external input: no occupied stages. */
        break;
    case 32:
        for (lane = 0; lane < 2; lane++) {
            used = fifo32_num_used(&c->output[(c->control & 2) ? lane : 0]);
            for (i = 0; i < used; i++) {
                *value |= 1ULL << (i * 2 + lane);
            }
        }
        break;
    case 0:
    case 40:
    case 44:
    case 56:
        /* An empty input read is a bus failure, never a fabricated sample. */
        return z7ii_audio_error(s, offset, size, false, 0);
    default:
        return z7ii_audio_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_audio_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size, MemTxAttrs attrs)
{
    Z7IIAudioState *s = opaque;
    Z7IIAudioChannel *c;
    unsigned lane = 0, format, i;
    uint32_t old;

    if (size != 4 || (offset & 3)) {
        return z7ii_audio_error(s, offset, size, true, value);
    }
    if (!offset) {
        if (value & ~3U) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        s->reset = value;
        for (i = 0; i < 2; i++) {
            if (value & (1U << i)) {
                z7ii_audio_channel_reset(&s->channel[i]);
            } else {
                z7ii_audio_update(&s->channel[i]);
                z7ii_audio_schedule(&s->channel[i]);
            }
        }
        return MEMTX_OK;
    }
    if (offset < 0x100 || offset >= 0x250) {
        return z7ii_audio_error(s, offset, size, true, value);
    }
    c = &s->channel[(offset - 0x100) >> 8];
    switch (offset & 0xff) {
    case 8:
        format = (value >> 24) & 127;
        if ((value & ~0xff8fff03U) ||
            (format && format != 0x44 && format != 0x4d) ||
            (((value >> 16) & 15) && ((value >> 16) & 15) != 0xa &&
             ((value >> 16) & 15) != 0xc) ||
            ((value & 2) && (value & (1U << 15)))) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        old = c->control;
        if (((old ^ value) & 2) && z7ii_audio_used(c)) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        /* INTI/INTO are derived; the original firmware commands bit12/13. */
        c->control = value & ~0x300U;
        if (!(value & (1U << 13))) {
            timer_del(c->timer);
        }
        break;
    case 12:
        /* Preserve original unknown bit23/26 flags. No swap/loopback packing. */
        if ((value & ~0x0f800000U) ||
            (((value ^ c->mode) & (1U << 25)) && z7ii_audio_used(c))) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        c->mode = value;
        break;
    case 16:
        if (value & ~0xff0f0000U) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        /* Error bits are W0C, status bits24/25 never produced by writes. */
        c->status = (value & 0xfc0c0000U) |
                    (c->status & value & 0x30000U);
        break;
    case 20:
        i = (value >> 24) & 31;
        if ((value & ~0xdf1f0000U) || (i && !(i & 1))) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        c->divider = value;
        timer_del(c->timer);
        break;
    case 24:
        /* No mix, pin routing or sample packing inferred from AUDP. */
        if (value) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        c->port = value;
        break;
    case 4:
    case 60:
        if (c->control & 2) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        goto output;
    case 52:
        lane = 1;
        /* fall through */
    case 48:
        if (!(c->control & 2)) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
    output:
        if ((s->reset & (1U << c->index)) ||
            fifo32_num_used(&c->output[lane]) >= z7ii_audio_capacity(c)) {
            return z7ii_audio_error(s, offset, size, true, value);
        }
        fifo32_push(&c->output[lane], value);
        break;
    default:
        return z7ii_audio_error(s, offset, size, true, value);
    }
    z7ii_audio_update(c);
    z7ii_audio_schedule(c);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_audio_ops = {
    .read_with_attrs = z7ii_audio_read,
    .write_with_attrs = z7ii_audio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_audio_reset(DeviceState *dev)
{
    Z7IIAudioState *s = Z7II_AUDIO(dev);
    unsigned i;

    s->reset = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    for (i = 0; i < 2; i++) {
        z7ii_audio_channel_reset(&s->channel[i]);
    }
}

static void z7ii_audio_init(Object *obj)
{
    Z7IIAudioState *s = Z7II_AUDIO(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-audio-clock");
    memory_region_init_io(&s->regs, obj, &z7ii_audio_ops, s,
                          "z7ii.audio", 0x250);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    qdev_init_gpio_out(DEVICE(obj), s->request, 3);
    for (i = 0; i < 2; i++) {
        Z7IIAudioChannel *c = &s->channel[i];

        c->owner = s;
        c->index = i;
        fifo32_create(&c->output[0], 16);
        fifo32_create(&c->output[1], 16);
        c->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, z7ii_audio_sink, c);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &c->irq);
    }
}

static void z7ii_audio_finalize(Object *obj)
{
    Z7IIAudioState *s = Z7II_AUDIO(obj);
    unsigned i;

    for (i = 0; i < 2; i++) {
        timer_free(s->channel[i].timer);
        fifo32_destroy(&s->channel[i].output[0]);
        fifo32_destroy(&s->channel[i].output[1]);
    }
}

static const VMStateDescription z7ii_audio_vmstate = {
    .name = "z7ii-audio", .unmigratable = true,
};

static void z7ii_audio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_audio_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_audio_reset);
}

static const TypeInfo z7ii_audio_type = {
    .name = TYPE_Z7II_AUDIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIAudioState),
    .instance_init = z7ii_audio_init,
    .instance_finalize = z7ii_audio_finalize,
    .class_init = z7ii_audio_class_init,
};

static void z7ii_audio_register_types(void)
{
    type_register_static(&z7ii_audio_type);
}
type_init(z7ii_audio_register_types)
