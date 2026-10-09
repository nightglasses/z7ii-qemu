/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Original EX's 24-bit I2C expander register protocol. The PCAL6524 software
 * profile does not identify the physical Nikon part. See docs/startup/ex.md.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/nikon/exio.h"
#include "migration/vmstate.h"

struct NikonEXIOState {
    I2CSlave parent_obj;
    uint8_t reg[0x77];
    uint8_t pointer;
    bool command;
    bool increment;
    bool reset_low;
    uint32_t external;
    uint32_t driven;
    uint32_t input;
    uint32_t raw;
    uint32_t reference;
    uint32_t latched;
    uint32_t latch_valid;
    uint32_t pending;
    uint8_t debounce[16];
    uint8_t clock_cycles;
    qemu_irq pad[NIKON_EX_IO_PINS];
    qemu_irq interrupt;
};

static const uint8_t groups[][2] = {
    { 0x00, 3 }, { 0x04, 3 }, { 0x08, 3 }, { 0x0c, 3 },
    { 0x40, 6 }, { 0x48, 3 }, { 0x4c, 3 }, { 0x50, 3 },
    { 0x54, 3 }, { 0x58, 3 }, { 0x5c, 1 }, { 0x60, 6 },
    { 0x68, 3 }, { 0x6c, 3 }, { 0x70, 3 }, { 0x74, 3 },
};

static int exio_group(unsigned reg)
{
    for (unsigned i = 0; i < ARRAY_SIZE(groups); i++) {
        if (reg >= groups[i][0] && reg < groups[i][0] + groups[i][1]) {
            return i;
        }
    }
    return -1;
}

static uint32_t exio_bits(NikonEXIOState *s, unsigned reg)
{
    return s->reg[reg] | (s->reg[reg + 1] << 8) | (s->reg[reg + 2] << 16);
}

static unsigned exio_edge(NikonEXIOState *s, unsigned pin)
{
    return (s->reg[0x60 + pin / 4] >> ((pin % 4) * 2)) & 3;
}

static uint32_t exio_status(NikonEXIOState *s)
{
    uint32_t result = s->pending;
    uint32_t value = (s->input & ~s->latch_valid) |
                     (s->latched & s->latch_valid);
    for (unsigned pin = 0; pin < 24; pin++) {
        if (!exio_edge(s, pin)) {
            result |= (value ^ s->reference) & (1U << pin);
        }
    }
    return result & exio_bits(s, 0x0c) & ~exio_bits(s, 0x54);
}

static void exio_irq(NikonEXIOState *s)
{
    qemu_set_irq(s->interrupt, s->reset_low || !exio_status(s));
}

static uint32_t exio_resolve(NikonEXIOState *s, bool drive_outputs)
{
    uint32_t result = 0;
    for (unsigned pin = 0; pin < 24; pin++) {
        unsigned port = pin / 8, bit = pin % 8;
        uint32_t mask = 1U << pin;
        bool input = s->reg[0x0c + port] & (1 << bit);
        bool output = s->reg[4 + port] & (1 << bit);
        bool drain = ((s->reg[0x5c] >> port) ^
                      (s->reg[0x70 + port] >> bit)) & 1;
        int drive = input || (drain && output) ? -1 : output;
        bool value;
        if (drive >= 0) {
            value = drive;
        } else if (s->driven & mask) {
            value = s->external & mask;
        } else if (s->reg[0x4c + port] & (1 << bit)) {
            value = s->reg[0x50 + port] & (1 << bit);
        } else {
            /* Explicit floating-net functional assumption, not readiness. */
            value = true;
        }
        if (drive_outputs) {
            qemu_set_irq(s->pad[pin], s->reset_low ? -1 : drive);
        }
        if (value) {
            result |= mask;
        }
    }
    return result;
}

static void exio_update(NikonEXIOState *s, bool clock)
{
    uint32_t raw = exio_resolve(s, true), next = raw;
    bool debounce = s->reg[0x76] && (s->reg[0x74] & 1) &&
                    (s->reg[0x0c] & 1);
    bool clock_ready = s->clock_cycles == 9;
    if (clock && debounce && s->clock_cycles < 9) {
        s->clock_cycles++;
    }
    for (unsigned pin = 1; pin < 16; pin++) {
        uint32_t mask = 1U << pin;
        if ((s->raw ^ raw) & mask) {
            s->debounce[pin] = 0;
        }
        bool enabled = debounce &&
                       (s->reg[0x74 + pin / 8] & (1 << (pin % 8))) &&
                       (s->reg[0x0c + pin / 8] & (1 << (pin % 8)));
        if (enabled && ((raw ^ s->input) & mask)) {
            if (clock && clock_ready) {
                s->debounce[pin]++;
            }
            if (s->debounce[pin] < s->reg[0x76]) {
                next = (next & ~mask) | (s->input & mask);
            } else {
                s->debounce[pin] = 0;
            }
        } else {
            s->debounce[pin] = 0;
        }
    }
    s->raw = raw;
    uint32_t changed = next ^ s->input;
    for (unsigned pin = 0; pin < 24; pin++) {
        uint32_t mask = 1U << pin;
        if (!(changed & mask) || !(exio_bits(s, 0x0c) & mask) ||
            (exio_bits(s, 0x54) & mask)) {
            continue;
        }
        unsigned edge = exio_edge(s, pin);
        if (edge == 3 || (edge == 1 && (next & mask)) ||
            (edge == 2 && !(next & mask))) {
            s->pending |= mask;
        }
        if ((exio_bits(s, 0x48) & mask) && !(s->latch_valid & mask)) {
            s->latched = (s->latched & ~mask) | (next & mask);
            s->latch_valid |= mask;
        }
    }
    s->input = next;
    exio_irq(s);
}

static void exio_clear(NikonEXIOState *s, uint32_t mask)
{
    s->pending &= ~mask;
    s->latch_valid &= ~mask;
    s->reference = (s->reference & ~mask) | (s->input & mask);
}

static void exio_next(NikonEXIOState *s)
{
    int group = exio_group(s->pointer);
    assert(group >= 0);
    if (++s->pointer == groups[group][0] + groups[group][1]) {
        unsigned next = s->increment ? (group + 1) % ARRAY_SIZE(groups) : group;
        s->pointer = groups[next][0];
    }
}

static uint8_t exio_recv(I2CSlave *dev)
{
    NikonEXIOState *s = NIKON_EX_IO(dev);
    unsigned reg = s->pointer;
    uint8_t value;
    if (reg <= 2 || (reg >= 0x6c && reg <= 0x6e)) {
        unsigned port = reg <= 2 ? reg : reg - 0x6c;
        uint32_t data = s->input;
        if (reg <= 2) {
            data = (data & ~s->latch_valid) | (s->latched & s->latch_valid);
        }
        uint8_t drain = ((s->reg[0x5c] & (1 << port)) ? 0xff : 0) ^
                         s->reg[0x70 + port];
        value = ((data >> (port * 8)) ^ s->reg[8 + port]) &
                ~(drain & ~s->reg[0x0c + port]);
        if (reg <= 2) {
            exio_clear(s, 0xffU << (port * 8));
        }
    } else if (reg >= 0x58 && reg <= 0x5a) {
        value = exio_status(s) >> ((reg - 0x58) * 8);
    } else if (reg >= 0x68 && reg <= 0x6a) {
        value = 0; /* Defined self-clearing command registers. */
    } else {
        value = s->reg[reg];
    }
    exio_next(s);
    exio_irq(s);
    return value;
}

static int exio_send(I2CSlave *dev, uint8_t data)
{
    NikonEXIOState *s = NIKON_EX_IO(dev);
    if (s->command) {
        if (exio_group(data & 0x7f) < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "EX IO: reserved command 0x%02x NACK\n", data);
            return -1;
        }
        s->pointer = data & 0x7f;
        s->increment = data & 0x80;
        s->command = false;
        return 0;
    }
    unsigned reg = s->pointer;
    if (reg >= 0x68 && reg <= 0x6a) {
        exio_clear(s, (uint32_t)data << ((reg - 0x68) * 8));
    } else if (reg > 2 && !(reg >= 0x58 && reg <= 0x5a) &&
               !(reg >= 0x6c && reg <= 0x6e)) {
        if (reg >= 0x60 && reg <= 0x65) {
            unsigned first = (reg - 0x60) * 4;
            for (unsigned n = 0; n < 4; n++) {
                if (((s->reg[reg] ^ data) >> (n * 2)) & 3) {
                    exio_clear(s, 1U << (first + n));
                }
            }
        }
        if (reg >= 0x74 && reg <= 0x76 && s->reg[reg] != data) {
            memset(s->debounce, 0, sizeof(s->debounce));
            s->clock_cycles = 0;
        }
        s->reg[reg] = reg == 0x5c ? data & 7 : data;
        if (reg >= 0x54 && reg <= 0x56) {
            exio_clear(s, (uint32_t)data << ((reg - 0x54) * 8));
        }
        s->pending &= exio_bits(s, 0x0c);
        s->latch_valid &= exio_bits(s, 0x48) & exio_bits(s, 0x0c);
    }
    exio_next(s);
    exio_update(s, false);
    return 0;
}

static int exio_event(I2CSlave *dev, enum i2c_event event)
{
    NikonEXIOState *s = NIKON_EX_IO(dev);
    if (s->reset_low) {
        return -1;
    }
    if (event == I2C_START_SEND) {
        s->command = true;
    }
    return 0;
}

static void exio_reset(NikonEXIOState *s, bool drive_outputs)
{
    memset(s->reg, 0, sizeof(s->reg));
    memset(s->reg + 4, 0xff, 3);
    memset(s->reg + 0x0c, 0xff, 3);
    memset(s->reg + 0x40, 0xff, 6);
    memset(s->reg + 0x50, 0xff, 3);
    memset(s->reg + 0x54, 0xff, 3);
    memset(s->debounce, 0, sizeof(s->debounce));
    s->pointer = 0;
    s->command = true;
    s->increment = false;
    s->pending = s->latched = s->latch_valid = 0;
    s->clock_cycles = 0;
    s->raw = s->input = s->reference = exio_resolve(s, drive_outputs);
    if (drive_outputs) {
        exio_irq(s);
    }
}

static void exio_reset_hold(Object *obj, ResetType type)
{
    exio_reset(NIKON_EX_IO(obj), false);
}

static void exio_reset_exit(Object *obj, ResetType type)
{
    NikonEXIOState *s = NIKON_EX_IO(obj);
    /* Publish the physical line only after the GPIO receiver has reset.
     * Its upstream hold phase disconnects all external inputs. */
    exio_resolve(s, true);
    exio_irq(s);
}

static void exio_input(void *opaque, int pin, int level)
{
    NikonEXIOState *s = opaque;
    uint32_t mask = 1U << pin;
    bool clock = pin == 0 && level > 0 && !(s->external & 1);
    s->driven = level < 0 ? s->driven & ~mask : s->driven | mask;
    s->external = level > 0 ? s->external | mask : s->external & ~mask;
    if (!s->reset_low) {
        exio_update(s, clock);
    }
}

static void exio_reset_input(void *opaque, int pin, int level)
{
    NikonEXIOState *s = opaque;
    bool low = !level;
    if (low && !s->reset_low) {
        s->reset_low = true;
        exio_reset(s, true);
    } else if (!low && s->reset_low) {
        s->reset_low = false;
        exio_update(s, false);
    }
}

static int exio_post_load(void *opaque, int version)
{
    NikonEXIOState *s = opaque;
    if (exio_group(s->pointer) < 0 || s->clock_cycles > 9 ||
        ((s->external | s->driven | s->input | s->raw | s->reference |
          s->latched | s->latch_valid | s->pending) & ~0xffffffU)) {
        return -EINVAL;
    }
    exio_resolve(s, true);
    exio_irq(s);
    return 0;
}

static const VMStateDescription exio_vmstate = {
    .name = TYPE_NIKON_EX_IO, .version_id = 1, .minimum_version_id = 1,
    .post_load = exio_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, NikonEXIOState),
        VMSTATE_UINT8_ARRAY(reg, NikonEXIOState, 0x77),
        VMSTATE_UINT8(pointer, NikonEXIOState),
        VMSTATE_BOOL(command, NikonEXIOState),
        VMSTATE_BOOL(increment, NikonEXIOState),
        VMSTATE_BOOL(reset_low, NikonEXIOState),
        VMSTATE_UINT32(external, NikonEXIOState),
        VMSTATE_UINT32(driven, NikonEXIOState),
        VMSTATE_UINT32(input, NikonEXIOState),
        VMSTATE_UINT32(raw, NikonEXIOState),
        VMSTATE_UINT32(reference, NikonEXIOState),
        VMSTATE_UINT32(latched, NikonEXIOState),
        VMSTATE_UINT32(latch_valid, NikonEXIOState),
        VMSTATE_UINT32(pending, NikonEXIOState),
        VMSTATE_UINT8_ARRAY(debounce, NikonEXIOState, 16),
        VMSTATE_UINT8(clock_cycles, NikonEXIOState),
        VMSTATE_END_OF_LIST()
    },
};

static void exio_init(Object *obj)
{
    NikonEXIOState *s = NIKON_EX_IO(obj);
    qdev_init_gpio_in_named(DEVICE(s), exio_input, "pad", 24);
    qdev_init_gpio_in_named(DEVICE(s), exio_reset_input, "reset", 1);
    qdev_init_gpio_out_named(DEVICE(s), s->pad, "pad-out", 24);
    qdev_init_gpio_out_named(DEVICE(s), &s->interrupt, "interrupt", 1);
}

static void exio_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *ic = I2C_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ic->recv = exio_recv;
    ic->send = exio_send;
    ic->event = exio_event;
    dc->vmsd = &exio_vmstate;
    dc->desc = "EX 24-bit I/O expander software functional profile";
    rc->phases.hold = exio_reset_hold;
    rc->phases.exit = exio_reset_exit;
}

static const TypeInfo exio_type = {
    .name = TYPE_NIKON_EX_IO, .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(NikonEXIOState), .instance_init = exio_init,
    .class_init = exio_class_init,
};

static void exio_register(void)
{
    type_register_static(&exio_type);
}
type_init(exio_register)
